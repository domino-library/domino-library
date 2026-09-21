/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>

#define IN_GTEST
#include "ThreadBack.hpp"
#undef IN_GTEST

#include "AsyncBack.hpp"
#include "MT_PingMainTH.hpp"
#include "MtInQueue.hpp"
#include "ThPoolBack.hpp"
#include "ThreadBackViaMsgSelf.hpp"
#include "UniLog.hpp"
#include "UtInitObjAnywhere.hpp"

using namespace testing;

namespace rlib
{
// ***********************************************************************************************
// - construct a ThreadBack with maxParallel=2 (must be a template so if-constexpr discards the
//   wrong-type branch; macro-expanded fixtures are non-template, where both branches must compile)
template<class T>
std::unique_ptr<T> mkBack_maxParallel2()
{
    if constexpr (std::is_same_v<T, ThPoolBack>)
        return std::make_unique<T>(1, 2);  // 1 thread, maxTaskQ=2
    else
        return std::make_unique<T>(2);     // max=2
}

// ***********************************************************************************************
inline void drainDoneFut(ThreadBack& back)
{
    // nFut()==0: done. handled some: try again now. handled 0: sleep until ping.
    while (back.nFut() > 0)
    {
        if (back.hdlDoneFut() == 0)
            timedwait();
    }
}

// ***********************************************************************************************
struct THREAD_BACK_TEST : public UtInitObjAnywhere
{
    THREAD_BACK_TEST() : threadBack_(*ObjAnywhere::getObj<THREAD_BACK_TYPE>().get())
    {
        mt_getMainTH();  // designate this (gtest) thread as the ONE logical main
        EXPECT_EQ(0, threadBack_.nFut()) << "REQ: clear env";
    }
    void dumpIfFail() override
    {
        if (!HasFailure()) return;
        std::cerr << "nFut=" << threadBack_.nFut()
            << " nQ=" << mt_getQ().size(true) << '\n';
        UtInitObjAnywhere::dumpIfFail();
    }
    void TearDown() override
    {
        dumpIfFail();
        if (isSoak() && HasFailure()) soakReplayAndAbort();
        drainDoneFut(threadBack_);
        threadBack_.mt_nDoneFut() = 0;  // bugFix case may leave a stale counter
        EXPECT_EQ(0, threadBack_.nFut()) << "REQ: handle all";
        mt_getQ().clearAll();
        UtInitObjAnywhere::TearDown();
    }

    // -------------------------------------------------------------------------------------------
    THREAD_BACK_TYPE& threadBack_;  // OA default pool
};

#define THREAD_AND_BACK
// ***********************************************************************************************
//                                  [main thread]
//                                       |
//                                       | std::async()    [new thread]
//               ThreadBack::newTaskOK() |--------------------->| MT_TaskEntryFN()
//   [future, TaskBackFN]->fut_backFN_S_ |                      |
//                                       |                      |
//                                       |                      |
//                                       |<.....................| future<>
//        ThreadBack::hdlDoneFut() |
//                                       |
TEST_F(THREAD_BACK_TEST, GOLD_entryFn_inNewThread_thenBackFn_inMainThread_withTimedWait)
{
    const auto mainTH = mt_getMainTH();
    EXPECT_EQ(mainTH, std::this_thread::get_id()) << "REQ: OK in main thread";
    EXPECT_TRUE(threadBack_.newTaskOK(
        // MT_TaskEntryFN
        [mainTH]()
        {
            EXPECT_NE(mainTH, std::this_thread::get_id()) << "REQ: in new thread";
            return make_safe<bool>(true);
        },
        // TaskBackFN
        [mainTH](SafePtr<void>)
        {
            EXPECT_EQ(mainTH, std::this_thread::get_id()) << "REQ: in main thread";
        }
    ));

    while (threadBack_.hdlDoneFut() == 0)
    {
        INF("new thread not end yet...");
        timedwait();  // REQ: timedwait() is more efficient than keep hdlDoneFut()
    }
}
TEST_F(THREAD_BACK_TEST, GOLD_entryFnResult_toBackFn_withoutTimedWait)
{
    const size_t maxThread = 2;  // req: test entryFn result true(succ) / false(fail)
    for (size_t idxThread = 0; idxThread < maxThread; ++idxThread)
    {
        SCOPED_TRACE(idxThread);
        EXPECT_TRUE(threadBack_.newTaskOK(
            // MT_TaskEntryFN
            [idxThread]()
            {
                return make_safe<bool>(idxThread % 2 != 0);  // ret true / false
            },
            // TaskBackFN
            [idxThread](SafePtr<void> aRet)
            {
                EXPECT_EQ(idxThread % 2 != 0, *(safe_cast<bool>(aRet).get())) << "REQ: check true & false";
            }
        ));
    }

    // REQ: no timedwait() but keep asking; cost most time in CI
    for (size_t nHandled = 0; nHandled < maxThread; nHandled += threadBack_.hdlDoneFut())
    {
        INF("nHandled=" << nHandled);
    }
}
TEST_F(THREAD_BACK_TEST, canHandle_someThreadDone_whileOtherRunning)
{
    std::atomic<bool> canEnd(false);
    EXPECT_TRUE(threadBack_.newTaskOK(
        // MT_TaskEntryFN
        [&canEnd]()
        {
            while (not canEnd)
                std::this_thread::yield();  // not end until instruction
            return make_safe<bool>(true);
        },
        // TaskBackFN
        [](SafePtr<void>) {}
    )) << "REQ: newTaskOK";

    EXPECT_TRUE(threadBack_.newTaskOK(
        // MT_TaskEntryFN
        []()
        {
            return make_safe<bool>(false);  // quick end
        },
        // TaskBackFN
        [](SafePtr<void>) {}
    )) << "REQ: newTaskOK";

    while (threadBack_.hdlDoneFut() == 0)
    {
        INF("both threads not end yet, wait...");
        timedwait();
    }

    canEnd = true;  // 1st thread keep running while 2nd is done
    while (threadBack_.hdlDoneFut() == 0)  // no timedwait() so keep occupy cpu
    {
        INF("2nd thread done, wait 1st done...");
        timedwait();
    }
}

#define WAIT_NOTIFY
// ***********************************************************************************************
TEST_F(THREAD_BACK_TEST, GOLD_entryFn_notify_insteadof_timeout)
{
    if (isSoak()) GTEST_SKIP() << "wall-clock upper bound vs nice";
    auto start = std::chrono::high_resolution_clock::now();
    EXPECT_TRUE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },  // entryFn
        [](SafePtr<void>) {}  // backFn
    )) << "REQ: newTaskOK";
    timedwait(0, 500'000'000);  // long timer to ensure thread done beforehand
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
    EXPECT_LT(dur.count(), 500) << "REQ: entryFn end shall notify g_notifMainTH instead of timeout";

    while (threadBack_.hdlDoneFut() == 0)  // clear all threads
        timedwait();
}

#define ABNORMAL
// ***********************************************************************************************
TEST_F(THREAD_BACK_TEST, GOLD_except_entryFN_backFN)
{
    std::atomic<int> step{0};
    EXPECT_TRUE(threadBack_.newTaskOK(
        [&step]  // entryFN()
        {
            ++step;
            throw std::runtime_error("entryFN() exception");
            return make_safe<bool>(true);
        },
        [&step](SafePtr<void> aRet)  // backFN()
        {
            EXPECT_EQ(1, step.load()) << "REQ: entryFN() executed";
            EXPECT_EQ(nullptr, aRet.get()) << "REQ: entryFN() except -> aRet=null";
            ++step;
            throw std::runtime_error("backFN() exception");
        }
    ));
    while (threadBack_.hdlDoneFut() < 1) {  // REQ: counter++ after entryFN() except
        timedwait();
    }
    EXPECT_EQ(2, step.load()) << "REQ: backFN() executed";
    EXPECT_EQ(0, threadBack_.nFut()) << "REQ: task removed from fut_backFN_S_ after exceptions";
    EXPECT_EQ(0, threadBack_.mt_nDoneFut().load()) << "REQ: ok after backFN() except";
}
TEST_F(THREAD_BACK_TEST, emptyThreadList_ok)
{
    size_t nHandled = threadBack_.hdlDoneFut();
    EXPECT_EQ(0u, nHandled);
}
TEST_F(THREAD_BACK_TEST, submit_wrongThread_rejected)
{
    bool wrongThreadNewOK = true;
    bool wrongThreadLimitOK = true;
    std::async(std::launch::async, [&]()
    {
        wrongThreadNewOK = threadBack_.newTaskOK(
            [] { return make_safe<bool>(true); },
            [](SafePtr<void>) {});
        wrongThreadLimitOK = threadBack_.limitNewTaskOK(
            [] { return make_safe<bool>(true); },
            [](SafePtr<void>) {});
    }).get();

    EXPECT_FALSE(wrongThreadNewOK) << "REQ: reject wrong-thread newTaskOK()";
    EXPECT_FALSE(wrongThreadLimitOK) << "REQ: reject wrong-thread limitNewTaskOK()";
    EXPECT_EQ(0u, threadBack_.nFut()) << "REQ: no wrong-thread task saved";
}
TEST_F(THREAD_BACK_TEST, hdlDoneFut_wrongThread_rejected)
{
    std::atomic<int> nBack{0};
    ASSERT_TRUE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },
        [&nBack](SafePtr<void>) { ++nBack; }));
    threadBack_.waitAllFut_forUt();
    while (threadBack_.mt_nDoneFut().load(std::memory_order_acquire) == 0)
        std::this_thread::yield();

    const auto wrongThreadHandled = std::async(std::launch::async, [&]()
    {
        return threadBack_.hdlDoneFut();
    }).get();

    EXPECT_EQ(0u, wrongThreadHandled) << "REQ: reject wrong-thread hdlDoneFut()";
    EXPECT_EQ(0, nBack.load()) << "REQ: callback remains on main thread";
    EXPECT_EQ(1u, threadBack_.nFut()) << "REQ: wrong-thread call keeps completed task";

    EXPECT_EQ(1u, threadBack_.hdlDoneFut()) << "REQ: main thread handles completed task";
    EXPECT_EQ(1, nBack.load());
}
TEST_F(THREAD_BACK_TEST, invalid_msgSelf_entryFN_backFN)
{
    auto kept = MSG_SELF;
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK<MsgSelf>(nullptr, *this));  // rm so viaMsgSelf sees absent

    EXPECT_FALSE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },  // entryFn
        viaMsgSelf([](SafePtr<void>) {})  // invalid since msgSelf==nullptr
    ));

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(kept, *this)) << "REQ: valid MSG_SELF";
    EXPECT_FALSE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },  // entryFn
        viaMsgSelf(nullptr)  // invalid since backFn==nullptr
    ));

    EXPECT_FALSE(threadBack_.newTaskOK(
        MT_TaskEntryFN(),  // invalid entryFn
        [](SafePtr<void>) {}  // backFn
    ));

    EXPECT_EQ(0, threadBack_.nFut());
}
TEST_F(THREAD_BACK_TEST, bugFix_nDoneFut_before_futureReady)
{
    std::atomic<bool> canEnd(false);
    EXPECT_TRUE(threadBack_.newTaskOK(
        // MT_TaskEntryFN
        [&canEnd]()
        {
            while (not canEnd)
                std::this_thread::yield();  // not end until instruction
            return make_safe<bool>(true);
        },
        // TaskBackFN
        [](SafePtr<void>) {}
    )) << "REQ: newTaskOK";

    threadBack_.mt_nDoneFut()++;  // force +1 before future ready, threadBack_ shall not crash

    // clean
    canEnd = true;
    while (threadBack_.hdlDoneFut() == 0)
        timedwait();
}

// - REQ: mt_getMainTH() returns the process's ONE logical main = the 1st caller; a later call from
//        ANY other thread still returns that SAME main id (the worker does NOT become main).
TEST_F(THREAD_BACK_TEST, getMainTH_1stCallerWins_otherThread_stillGetsSameMainId)
{
    const auto mainTH = mt_getMainTH();  // 1st caller (gtest thread) is fixed as main
    EXPECT_EQ(mainTH, std::this_thread::get_id()) << "REQ: 1st caller is the logical main";

    std::atomic<bool> otherBecameMain(true);
    std::thread other([mainTH, &otherBecameMain]
    {
        const auto ret = mt_getMainTH();  // from another thread -> same main id (worker NOT main)
        EXPECT_EQ(mainTH, ret) << "REQ: still the 1st main, NOT this worker";
        otherBecameMain = (ret == std::this_thread::get_id());
    });
    other.join();

    EXPECT_FALSE(otherBecameMain.load()) << "REQ: other thread did NOT become main";
    EXPECT_EQ(mainTH, mt_getMainTH())    << "REQ: main stays the 1st designated thread";
}
// - REQ: mt_reqMainTH() is the main-thread guard: true in main, false (with alarm) in any other thread.
TEST_F(THREAD_BACK_TEST, reqMainTH_trueInMain_falseInOtherThread)
{
    mt_getMainTH();  // fix gtest thread as the logical main
    EXPECT_TRUE(mt_reqMainTH(__func__)) << "REQ: pass in main thread";

    std::atomic<bool> retInOther(true);
    std::thread other([&retInOther]{ retInOther = mt_reqMainTH("otherThread"); });
    other.join();
    EXPECT_FALSE(retInOther.load()) << "REQ: fail (alarm) in non-main thread";
}

#define NOTIFIER_TEST
// ***********************************************************************************************
TEST_F(THREAD_BACK_TEST, GOLD_integrate_MsgSelf_ThreadBack_MtInQueue)  // simulate real world
{
    std::set<std::string> cb_info;
    auto ms = MSG_SELF;  // same obj viaMsgSelf uses

    // setup msg handler table for mt_getQ()
    EXPECT_EQ(0u, mt_getQ().nHdlr())  << "REQ: init no hdlr";
    EXPECT_TRUE(mt_getQ().setHdlrOK<std::string>([ms, &cb_info](UniPtr aMsg)
    {
        EXPECT_TRUE(ms->newMsgOK(  // REQ: via MsgSelf
            [aMsg, &cb_info]
            {
                EXPECT_EQ("a", *(STATIC_PTR_CAST<std::string>(aMsg).get()));
                cb_info.emplace("REQ: a's Q hdlr via MsgSelf");
            }
        )) << "REQ: enqueue msg";
    })) << "REQ: set hdlr";
    EXPECT_EQ(1u, mt_getQ().nHdlr())  << "REQ: count hdlr";
    EXPECT_TRUE(mt_getQ().setHdlrOK<int>([ms, &cb_info](UniPtr aMsg)
    {
        EXPECT_TRUE(ms->newMsgOK(
            [aMsg, &cb_info]
            {
                EXPECT_EQ(2, *(STATIC_PTR_CAST<int>(aMsg).get()));
                cb_info.emplace("REQ: 2's Q hdlr via MsgSelf");
            }
        )) << "REQ: enqueue msg";
    })) << "REQ: set hdlr";
    EXPECT_EQ(2u, mt_getQ().nHdlr())  << "REQ: count hdlr";

    // push
    EXPECT_TRUE(threadBack_.newTaskOK(
        // entryFn
        [] {
            EXPECT_TRUE(mt_getQ().mt_pushOK(MAKE_PTR<std::string>("a"))) << "REQ: push OK";
            return make_safe<bool>(true);
        },
        // backFn
        viaMsgSelf(  // REQ: via MsgSelf
            [this, &cb_info](SafePtr<void> aRet)
            {
                EXPECT_TRUE(*(safe_cast<bool>(aRet).get())) << "entryFn succ";
                cb_info.emplace("REQ: a's backFn via MsgSelf");
            }
        )
    )) << "REQ: newTaskOK";
    EXPECT_TRUE(threadBack_.newTaskOK(
        // entryFn
        [] {
            EXPECT_TRUE(mt_getQ().mt_pushOK(MAKE_PTR<int>(2))) << "REQ: push OK";
            return make_safe<bool>(true);
        },
        // backFn
        viaMsgSelf(
            [this, &cb_info](SafePtr<void> aRet)
            {
                EXPECT_TRUE(*(safe_cast<bool>(aRet).get())) << "entryFn succ";
                cb_info.emplace("REQ: 2's backFn via MsgSelf");
            }
        )
    )) << "REQ: newTaskOK";

    // simulate main()
    const std::set<std::string> expect = {"REQ: a's Q hdlr via MsgSelf", "REQ: a's backFn via MsgSelf",
        "REQ: 2's Q hdlr via MsgSelf", "REQ: 2's backFn via MsgSelf"};
    for (;;)
    {
        // handle all done Thread
        INF("nMsg=" << ms->nMsg() << ", nQ=" << mt_getQ().size(true) << ", nTh=" << threadBack_.nFut());
        auto handled = threadBack_.hdlDoneFut();
        (void)handled;

        // handle all existing in mt_getQ()
        INF("nMsg=" << ms->nMsg() << ", nQ=" << mt_getQ().size(true) << ", nTh=" << threadBack_.nFut());
        mt_getQ().handleAllEle();

        INF("nMsg=" << ms->nMsg() << ", nQ=" << mt_getQ().size(true) << ", nTh=" << threadBack_.nFut());
        ms->handleAllMsg();

        INF("nMsg=" << ms->nMsg() << ", nQ=" << mt_getQ().size(true) << ", nTh=" << threadBack_.nFut());
        if (expect == cb_info)
            return;

        INF("nMsg=" << ms->nMsg() << ", nQ=" << mt_getQ().size(true) << ", nTh=" << threadBack_.nFut());
        timedwait();
    }
}
TEST_F(THREAD_BACK_TEST, timeout)
{
    g_notifMainTH.reset();  // drop leftover posts from prior cases (soak keeps the notifier)
    timedwait(0, size_t(-1));  // REQ: invalid ns>=1000ms, no die (& clear previous mt_notify if existed)

    auto now = std::chrono::high_resolution_clock::now();
    INF("start");
    timedwait();
    INF("end");
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - now);
    EXPECT_GE(dur.count(), 100) << "REQ: default timeout=100ms";

    timedwait(0, 0);  // REQ: immediate timeout to inc cov
}

#define CONSTRUCTOR_PARAM
// ***********************************************************************************************
TEST_F(THREAD_BACK_TEST, maxThread_0_forced_to_default)
{
    THREAD_BACK_TYPE inst(0);  // 0 forced to default
    EXPECT_TRUE(inst.newTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: can work with defaulted param";
    while (inst.hdlDoneFut() == 0)
        timedwait();
}
TEST_F(THREAD_BACK_TEST, maxThread_exceed_default)
{
    THREAD_BACK_TYPE inst(101);  // user can exceed default
    EXPECT_TRUE(inst.newTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    ));
    while (inst.hdlDoneFut() == 0)
        timedwait();
}
TEST_F(THREAD_BACK_TEST, limitMax_stays_at_constructMax_evenAfter_nonLimit_grow)
{
    // construct with maxParallel=2 (AsyncBack: max async#; ThPoolBack: maxTaskQ)
    auto back = mkBack_maxParallel2<THREAD_BACK_TYPE>();

    // grow fut_backFN_S_ beyond reserve(2) via non-limit newTaskOK -> vector reallocates
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(back->newTaskOK(
            [] { return make_safe<bool>(true); },
            [](SafePtr<void>) {}
        )) << "REQ: non-limit newTaskOK allows exceeding max";
    EXPECT_EQ(3u, back->nFut()) << "REQ: 3 saved, capacity reallocated past reserve(2)";

    // REQ: limit must still honor construct max=2, NOT the grown vector capacity
    EXPECT_FALSE(back->limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: reject since nFut(3) >= constructMax(2)";

    drainDoneFut(*back);
}

}  // namespace
