/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************

#include "MtInQueue.hpp"
#include "ThreadBackViaMsgSelf.hpp"
#include "UtInitObjAnywhere.hpp"

// Includers define these before the include. Defaults make the header compile on its own.
#ifndef THREAD_BACK_TYPE
#define THREAD_BACK_TYPE AsyncBack
#define THREAD_BACK_TEST ThreadBackTest
#endif

using namespace testing;

namespace rlib
{
// ***********************************************************************************************
// - construct with maxInFlight=2 (must be a template so if-constexpr discards the
//   wrong-type branch; macro-expanded fixtures are non-template, where both branches must compile)
//   AsyncBack: max async tasks. ThPoolBack: 1 thread, maxTaskQ=2.
template<class T>
std::unique_ptr<T> mkBack_maxInFlight2()
{
    if constexpr (std::is_same_v<T, ThPoolBack>)
        return std::make_unique<T>(1, 2);
    else
        return std::make_unique<T>(2);
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
    void cleanup_() override
    {
        drainDoneFut(threadBack_);
        EXPECT_EQ(0, threadBack_.nFut()) << "REQ: handle all";
        mt_getQ().clearAll();
    }

    // -------------------------------------------------------------------------------------------
    THREAD_BACK_TYPE& threadBack_;  // OA default pool
};

#define THREAD_AND_BACK
// ***********************************************************************************************
// Shared contract. AsyncBack ships the result in a future (picture). ThPoolBack posts it to a
// done list; hdlDoneFut is virtual and drains that list. backFN still runs on the main thread.
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
    while (threadBack_.hdlDoneFut() < 1) {  // REQ: completion is posted even when entryFN throws
        timedwait();
    }
    EXPECT_EQ(2, step.load()) << "REQ: backFN() executed";
    EXPECT_EQ(0, threadBack_.nFut()) << "REQ: task removed after exceptions";
}
TEST_F(THREAD_BACK_TEST, viaMsgSelf_backFN_except_caught)
{
    std::atomic<int> step{0};
    EXPECT_TRUE(threadBack_.newTaskOK(
        [&step]
        {
            ++step;
            return make_safe<bool>(true);
        },
        viaMsgSelf([&step](SafePtr<void> aRet)
        {
            EXPECT_EQ(1, step.load()) << "REQ: entryFN() executed";
            EXPECT_NE(nullptr, aRet.get());
            ++step;
            throw std::runtime_error("viaMsgSelf backFN exception");
        })
    ));
    while (threadBack_.hdlDoneFut() < 1)
        timedwait();
    EXPECT_EQ(0, threadBack_.nFut());
    ASSERT_EQ(1u, MSG_SELF->nMsg()) << "REQ: backFN queued, not run inside hdlDoneFut";
    MSG_SELF->handleAllMsg();  // REQ: backFN throw is caught inside the MsgCB
    EXPECT_EQ(2, step.load()) << "REQ: backFN ran";
    EXPECT_EQ(0u, MSG_SELF->nMsg());
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
TEST_F(THREAD_BACK_TEST, viaMsgSelf_invalidPri_newMsgOK_fails_noCrash)
{
    EXPECT_TRUE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },
        viaMsgSelf([](SafePtr<void>) {}, static_cast<EMsgPriority>(EMsgPri_MAX))
    )) << "REQ: wrap itself ok (MsgSelf present, backFN present)";

    while (threadBack_.hdlDoneFut() == 0)
        timedwait();

    EXPECT_EQ(0u, MSG_SELF->nMsg()) << "REQ: invalid pri not queued; no crash";
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
    // construct with maxInFlight=2 (AsyncBack: max async tasks; ThPoolBack: maxTaskQ)
    auto back = mkBack_maxInFlight2<THREAD_BACK_TYPE>();

    // non-limit newTaskOK may pass the construct max; limitNewTaskOK must still use that max
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(back->newTaskOK(
            [] { return make_safe<bool>(true); },
            [](SafePtr<void>) {}
        )) << "REQ: non-limit newTaskOK allows exceeding max";
    EXPECT_EQ(3u, back->nFut()) << "REQ: 3 saved, past construct max";

    // REQ: limit must still honor construct max=2, NOT the grown vector capacity
    EXPECT_FALSE(back->limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: reject since nFut(3) >= constructMax(2)";

    drainDoneFut(*back);
}
TEST_F(THREAD_BACK_TEST, dtor_discards_ready_backFN)
{
    std::atomic<int> nBack{0};
    {
        THREAD_BACK_TYPE back(1);
        EXPECT_TRUE(back.newTaskOK(
            [] { return make_safe<bool>(true); },
            [&](SafePtr<void>) { nBack += 1; }
        ));
        back.waitAllFut_forUt();  // ready, not yet pumped
    }
    EXPECT_EQ(0, nBack.load()) << "REQ: dtor does not run a ready backFN";
}
// backFN may submit. That completion can land before backFN returns, and this call must not run it.
// AsyncBack caps the call at the done-count it loaded on entry. ThPoolBack drains a swapped snapshot.
// waitAllFut_forUt from this backFN is safe only because the snapshot holds this one task:
// its slot is already freed, so nFut() is 0 before the nested submit and 1 after.
TEST_F(THREAD_BACK_TEST, hdlDoneFut_doesNotDrain_taskSubmittedByBackFN)
{
    std::atomic<int> nested{0};
    EXPECT_TRUE(threadBack_.newTaskOK(
        [] { return make_safe<bool>(true); },
        [&](SafePtr<void>)
        {
            if (threadBack_.nFut() != 0)
            {
                ADD_FAILURE() << "precond: snapshot holds only this task";
                return;  // else ThPoolBack::waitAllFut_forUt deadlocks
            }
            EXPECT_TRUE(threadBack_.newTaskOK(
                [] { return make_safe<int>(7); },
                [&](SafePtr<void> aRet) { nested = *(safe_cast<int>(aRet).get()); }
            )) << "REQ: backFN may submit";
            if (threadBack_.nFut() != 1)
            {
                ADD_FAILURE() << "precond: only the nested task is in flight";
                return;
            }
            threadBack_.waitAllFut_forUt();  // nested entry has published; its backFN has not run
            EXPECT_EQ(0, nested.load()) << "REQ: nested backFN stays out of this hdlDoneFut";
        }
    )) << "REQ: newTaskOK";

    size_t nHandled = 0;
    while ((nHandled = threadBack_.hdlDoneFut()) == 0)
        timedwait();
    EXPECT_EQ(1u, nHandled) << "REQ: this call handles only the task it already had";
    EXPECT_EQ(0, nested.load());
    EXPECT_EQ(1u, threadBack_.nFut()) << "REQ: nested task still in flight";

    while ((nHandled = threadBack_.hdlDoneFut()) == 0)
        timedwait();
    EXPECT_EQ(1u, nHandled);
    EXPECT_EQ(7, nested.load()) << "REQ: the next call runs the nested backFN";
}
// Slot is freed before backFN. At the construct max, limitNewTaskOK from the callback still accepts.
TEST_F(THREAD_BACK_TEST, limitNewTaskOK_fromBackFN_seesFreedSlot)
{
    auto back = mkBack_maxInFlight2<THREAD_BACK_TYPE>();
    std::atomic<int> nAccepted{0};
    std::atomic<int> nNestedRan{0};
    for (int i = 0; i < 2; ++i)
    {
        EXPECT_TRUE(back->limitNewTaskOK(
            [] { return make_safe<bool>(true); },
            [&](SafePtr<void>)
            {
                if (back->limitNewTaskOK(
                    [&] { nNestedRan += 1; return make_safe<bool>(true); },
                    [](SafePtr<void>) {}
                ))
                    nAccepted += 1;
            }
        )) << "REQ: fill to construct max";
    }
    back->waitAllFut_forUt();
    EXPECT_EQ(2u, back->hdlDoneFut()) << "REQ: both fillers are handled, nested tasks are not";
    EXPECT_EQ(2, nAccepted.load()) << "REQ: each backFN saw its slot already freed";
    EXPECT_EQ(2u, back->nFut()) << "REQ: the two accepted tasks are still in flight";
    drainDoneFut(*back);
    EXPECT_EQ(2, nNestedRan.load()) << "REQ: accepted tasks ran";
}

}  // namespace
