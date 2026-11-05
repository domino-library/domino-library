/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <cstdlib>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <queue>
#include <thread>
#include <unistd.h>

#include "MT_Notifier.hpp"
#include "UniLog.hpp"
#include "UtSoak.hpp"

#include "MT_PingMainTH.hpp"
#include "MtInQueue.hpp"

using namespace std;
using namespace testing;

namespace rlib
{
// ***********************************************************************************************
struct MtInQueueTest : public Test, public UniLog
{
    MtInQueueTest()
        : UniLog(UnitTest::GetInstance()->current_test_info()->name())
    {
        mt_getMainTH();  // designate this (gtest) thread as the logical main (timedwait asserts it)
    }
    void TearDown() override
    {
        if (HasFailure())
            std::cerr << "nQ=" << mt_getQ().size(true)
                << " nHdlr=" << mt_getQ().nHdlr() << '\n';
        if (isSoak() && HasFailure()) soakReplayAndAbort();
        mt_getQ().clearAll();  // same process Q; next case sees empty
    }
    ~MtInQueueTest()
    {
        GTEST_LOG_FAIL
        finishUniLogAfterCase(HasFailure());
    }
};

#define FIFO
// ***********************************************************************************************
TEST_F(MtInQueueTest, GOLD_simple_fifo)
{
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(2)));
    EXPECT_EQ(1, *(mt_getQ().pop<int>().get())) << "REQ: fifo";
    EXPECT_EQ(2, *(mt_getQ().pop<int>().get())) << "REQ: fifo";
}
TEST_F(MtInQueueTest, GOLD_sparsePush_fifo)
{
    const int nMsg = 10000;
    auto push_thread = async(launch::async, [&nMsg]()
    {
        for (int i = 0; i < nMsg; i++)
        {
            if (!mt_getQ().mt_pushOK(new_safe<int>(i)))
            {
                ADD_FAILURE() << "REQ: push OK";
                return;
            }
            this_thread::sleep_for(1us);  // simulate real world (sparse msg)
        }
    });

    int nHdl = 0;
    while (nHdl < nMsg)
    {
        auto msg = mt_getQ().pop<int>();  // CAN'T directly get() that destruct shared_ptr afterward
        if (msg.get())
        {
            //HID("nHdl=" << nHdl << ", msg=" << msg.get() << ", nRef=" << msg.use_count());
            SCOPED_TRACE(nHdl);
            EXPECT_EQ(nHdl, *(msg.get())) << "REQ: fifo";
            ++nHdl;
        }
        else
        {
            //HID("nHdl=" << nHdl << ", nRef=" << msg.use_count());
            timedwait();  // REQ: less CPU than repeat pop() or this_thread::yield()
        }
    }
    INF("REQ(sleep 1us/push): e2e user=0.354s->0.123s, sys=0.412s->0.159s");
}
TEST_F(MtInQueueTest, GOLD_surgePush_fifo)
{
    const int nMsg = 10000;
    auto push_thread = async(launch::async, [&nMsg]()
    {
        for (int i = 0; i < nMsg; i++)
        {
            if (!mt_getQ().mt_pushOK(new_safe<int>(i)))  // surge push
            {
                ADD_FAILURE() << "REQ: push OK";
                return;
            }
        }
    });

    int nHdl = 0;
    INF("before loop");
    while (nHdl < nMsg)
    {
        auto msg = mt_getQ().pop<int>();
        if (msg.get())
        {
            SCOPED_TRACE(nHdl);
            EXPECT_EQ(nHdl++, *(msg.get())) << "REQ: fifo";
        }
        else this_thread::yield();  // REQ: test cache_ performance
    }
    INF("REQ: loop cost 2576us now, previously no cache & lock cost 4422us");
}

#define PUSH
// ***********************************************************************************************
TEST_F(MtInQueueTest, pushWakeup_popNoBlockAndWakeup)
{
    EXPECT_TRUE(mt_getQ().mt_pushOK(new_safe<string>("1st"))) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK(new_safe<string>("2nd"))) << "REQ: push OK";
    {
        auto guard = mt_getQ().lockBackdoor();
        timedwait(600);  // REQ: not blocked 10min but waked by prev push

        ASSERT_EQ(nullptr, mt_getQ().pop<int>().get());
        timedwait(600);  // REQ: not blocked but waked by pop() that can't access queue_
    }
    ASSERT_EQ("1st", *(mt_getQ().pop<string>().get())) << "REQ: timedwait() not blocked but waked by failed pop()";

    {
        auto guard = mt_getQ().lockBackdoor();
        ASSERT_EQ("2nd", *(mt_getQ().pop<string>().get())) << "REQ: no-blocked pop from cache";
    }
}
TEST_F(MtInQueueTest, push_null_NOK)
{
    EXPECT_FALSE(mt_getQ().mt_pushOK<int>(SafePtr<int>())) << "REQ: push null int NOK";
    EXPECT_EQ(0, mt_getQ().size(true)) << "REQ: can't push nullptr since pop empty will ret nullptr";
}
TEST_F(MtInQueueTest, push_takeover_toEnsureMtSafe)
{
    auto ele = new_safe<int>(1);
    auto e2  = ele;
    EXPECT_FALSE(mt_getQ().mt_pushOK<int>(move(ele))) << "REQ: push failed since can't takeover ele";
    EXPECT_EQ(0, mt_getQ().size(true)) << "REQ: push failed since can't takeover ele";

    e2 = nullptr;
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(move(ele))) << "REQ: push succ since takeover";
    EXPECT_EQ(1, mt_getQ().size(true)) << "REQ: push succ since takeover";
    EXPECT_EQ(0, ele.use_count())      << "REQ: own nothing after push";
}
// use_count() ignores weak refs; handover must still cut them
TEST_F(MtInQueueTest, push_safeWeak_cannotReachQueuedEle)
{
    auto ele = new_safe<int>(7);
    SafeWeak<int> weak(ele);
    std::weak_ptr<int> stdWeak;
    {
        auto sp = ele.get();  // the shared_ptr get() hands out
        stdWeak = sp;
    }
    ASSERT_NE(nullptr, weak.lock().get()) << "fixture: SafeWeak reaches ele before push";
    ASSERT_FALSE(stdWeak.expired()) << "fixture: weak_ptr reaches ele before push";

    ASSERT_TRUE(mt_getQ().mt_pushOK<int>(move(ele))) << "REQ: sole new_safe can push";
    EXPECT_EQ(0, ele.use_count()) << "REQ: sender's SafePtr is taken";
    EXPECT_EQ(nullptr, weak.lock().get()) << "REQ: SafeWeak taken before push can't reach the queued ele";
    EXPECT_TRUE(stdWeak.expired()) << "REQ: weak_ptr from get() can't reach the queued ele";
    auto popped = mt_getQ().pop<int>();
    EXPECT_EQ(7, *popped.get()) << "REQ: queued value intact";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(move(popped))) << "REQ: popped ele can be pushed again";
}
// make_safe's object lives in its ctrl blk, so it can't be handed over
TEST_F(MtInQueueTest, push_makeSafe_NOK)
{
    auto ele = make_safe<int>(7);
    SafeWeak<int> weak(ele);
    ASSERT_FALSE(mt_getQ().mt_pushOK<int>(move(ele))) << "REQ: make_safe can't be pushed";
    EXPECT_EQ(1, ele.use_count()) << "REQ: rejected push leaves the SafePtr";
    EXPECT_EQ(7, *ele.get()) << "REQ: value untouched";
    EXPECT_EQ(7, *weak.lock().get()) << "REQ: SafeWeak still reaches it";
    EXPECT_EQ(0u, mt_getQ().size(true));
}
// handover keeps the object (no move), so base/immovable/2nd-base all work
TEST_F(MtInQueueTest, push_handover_keepsObject)
{
    // derived as base; base has no virtual dtor, so only the real deleter is correct
    {
        struct BaseNV { int x_ = 1; };
        struct DerivedNV : BaseNV
        {
            int* hits_;
            explicit DerivedNV(int& aHits) : hits_(&aHits) {}
            ~DerivedNV() { ++*hits_; }
        };
        int hits = 0;
        auto ele = new_safe<DerivedNV>(hits);
        SafeWeak<DerivedNV> weak(ele);
        ASSERT_TRUE(mt_getQ().mt_pushOK<BaseNV>(SafePtr<BaseNV>(move(ele))));
        EXPECT_EQ(0, hits) << "REQ: push keeps the derived object (no slice)";
        EXPECT_EQ(nullptr, weak.lock().get()) << "REQ: SafeWeak cut on base push";
        EXPECT_EQ(1, mt_getQ().pop<BaseNV>().get()->x_);
        EXPECT_EQ(1, hits) << "REQ: ~DerivedNV runs once";
    }
    // second base: stored address is offset from the created object
    {
        struct B1 { char pad_[32] = {}; };
        struct B2 { int b_ = 2; };
        struct Mid : B1, B2
        {
            int* hits_;
            explicit Mid(int& aHits) : hits_(&aHits) {}
            ~Mid() { ++*hits_; }
        };
        int hits = 0;
        auto ele = new_safe<Mid>(hits);
        SafeWeak<Mid> weak(ele);
        ASSERT_TRUE(mt_getQ().mt_pushOK<B2>(SafePtr<B2>(move(ele))));
        EXPECT_EQ(0, hits) << "REQ: push keeps Mid (no slice)";
        EXPECT_EQ(nullptr, weak.lock().get()) << "REQ: SafeWeak cut on 2nd-base push";
        EXPECT_EQ(2, mt_getQ().pop<B2>().get()->b_);
        EXPECT_EQ(1, hits) << "REQ: ~Mid runs once";
    }
    // not move-constructible
    {
        struct Imm
        {
            int v_;
            explicit Imm(int aV) : v_(aV) {}
            Imm(const Imm&) = delete;
            Imm(Imm&&) = delete;
        };
        auto ele = new_safe<Imm>(5);
        SafeWeak<Imm> weak(ele);
        ASSERT_TRUE(mt_getQ().mt_pushOK<Imm>(move(ele)));
        EXPECT_EQ(nullptr, weak.lock().get()) << "REQ: SafeWeak cut without moving Imm";
        EXPECT_EQ(5, mt_getQ().pop<Imm>().get()->v_);
    }
}

#define POP
// ***********************************************************************************************
TEST_F(MtInQueueTest, popEmpty)
{
    EXPECT_EQ(0u, mt_getQ().size(true)) << "empty MtQ";
    EXPECT_EQ(nullptr, mt_getQ().pop<int>().get()) << "REQ: can pop empty";
    EXPECT_EQ(nullptr, mt_getQ().pop().get()) << "REQ: can pop empty (diff pop)";
}
TEST_F(MtInQueueTest, popMismatchType)
{
    struct FlagObj
    {
        bool* gone_;  // bool*, not &: a moved-from object's destructor must not set the flag
        explicit FlagObj(bool& aGone) : gone_(&aGone) { *gone_ = false; }
        FlagObj(FlagObj&& aSrc) noexcept : gone_(aSrc.gone_) { aSrc.gone_ = nullptr; }
        ~FlagObj() { if (gone_) *gone_ = true; }
    };
    bool gone = false;
    EXPECT_TRUE(mt_getQ().mt_pushOK(new_safe<FlagObj>(gone))) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(11))) << "REQ: next ele";
    ASSERT_FALSE(gone);

    EXPECT_EQ(nullptr, mt_getQ().pop<int>().get()) << "REQ: type mismatch returns nullptr";
    EXPECT_FALSE(gone) << "REQ: mismatch leaves that element";
    EXPECT_EQ(2u, mt_getQ().size(true)) << "REQ: both ele remain";

    {
        auto head = mt_getQ().pop();
        EXPECT_NE(nullptr, head.get()) << "REQ: untyped pop takes the mismatched head";
    }
    EXPECT_TRUE(gone) << "REQ: element lives until that pop drops it";
    EXPECT_EQ(11, *mt_getQ().pop<int>().get()) << "REQ: later ele still there";
}

#define SIZE_Q
// ***********************************************************************************************
TEST_F(MtInQueueTest, sizeQ_block_nonBlock)
{
    ASSERT_EQ(0u, mt_getQ().size(true )) << "REQ: blocked init";
    ASSERT_EQ(0u, mt_getQ().size(false)) << "REQ: unblocked init";

    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    ASSERT_EQ(1u, mt_getQ().size(true )) << "REQ: inc blocked size";
    ASSERT_EQ(1u, mt_getQ().size(false)) << "REQ: inc unblocked size";

    {
        auto guard = mt_getQ().lockBackdoor();
        ASSERT_EQ(0u, mt_getQ().size(false)) << "REQ: sizeQ can be unblocked (blocked will hang)";
    }

    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(2))) << "REQ: push OK";
    EXPECT_EQ(1, *mt_getQ().pop<int>().get());
    ASSERT_EQ(1u, mt_getQ().size(true )) << "REQ: dec blocked size";
    ASSERT_EQ(1u, mt_getQ().size(false)) << "REQ: dec unblocked size";

    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(3))) << "REQ: push OK";
    ASSERT_EQ(2u, mt_getQ().size(true )) << "REQ: re-inc blocked size (now 1 in cache_, 1 in queue)";
    ASSERT_EQ(2u, mt_getQ().size(false)) << "REQ: re-inc unblocked size";

    {
        auto guard = mt_getQ().lockBackdoor();
        ASSERT_EQ(1u, mt_getQ().size(false)) << "REQ: sizeQ can be unblocked (get cache_ only)";
    }
}
TEST_F(MtInQueueTest, mainThread_boundaries_wrongThread_rejected)
{
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1)));
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(2)));
    EXPECT_EQ(1, *mt_getQ().pop<int>().get());  // 1 in cache_
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(3)));  // 1 in mt_queue_
    EXPECT_TRUE(mt_getQ().setHdlrOK<int>([](SafePtr<void>){}));

    size_t wrongThreadSize = size_t(-1);
    size_t wrongThreadHandled = size_t(-1);
    bool wrongThreadSetHdlr = true;
    async(launch::async, [&]()
    {
        wrongThreadSize = mt_getQ().size(true);
        wrongThreadSetHdlr = mt_getQ().setHdlrOK<bool>([](SafePtr<void>){});
        wrongThreadHandled = mt_getQ().handleAllEle();
        mt_getQ().clearAll();
    }).get();

    EXPECT_EQ(0u, wrongThreadSize) << "REQ: reject wrong-thread size() before touching cache_";
    EXPECT_FALSE(wrongThreadSetHdlr) << "REQ: reject wrong-thread setHdlrOK()";
    EXPECT_EQ(0u, wrongThreadHandled) << "REQ: reject wrong-thread handleAllEle()";
    EXPECT_EQ(2u, mt_getQ().size(true)) << "REQ: wrong-thread clearAll() changes nothing";
    EXPECT_EQ(1u, mt_getQ().nHdlr()) << "REQ: wrong-thread calls keep handlers";
}

#define DESTRUCT
// ***********************************************************************************************
struct LifeEle
{
    bool* isDestructed_;  // bool*, not &: a moved-from object's destructor must not set the flag
    explicit LifeEle(bool& aExtFlag) : isDestructed_(&aExtFlag) { *isDestructed_ = false; }
    LifeEle(LifeEle&& aSrc) noexcept : isDestructed_(aSrc.isDestructed_) { aSrc.isDestructed_ = nullptr; }
    ~LifeEle() { if (isDestructed_) *isDestructed_ = true; }
};
TEST_F(MtInQueueTest, destruct_right_type)
{
    bool isDestructed;
    EXPECT_TRUE(mt_getQ().mt_pushOK(new_safe<LifeEle>(isDestructed))) << "REQ: push OK";
    ASSERT_FALSE(isDestructed);

    mt_getQ().clearAll();
    ASSERT_TRUE(isDestructed) << "REQ: destruct correctly";
}
TEST_F(MtInQueueTest, clear_queue_cache_hdlr)
{
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(2))) << "REQ: push OK";
    auto popped = mt_getQ().pop();
    EXPECT_NE(nullptr, popped.get()) << "REQ: pop from cache";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(3))) << "REQ: push OK";

    {
        auto guard = mt_getQ().lockBackdoor();
        ASSERT_EQ(1u, mt_getQ().size(false)) << "1 in cache_";
    }
    EXPECT_EQ(2u, mt_getQ().size(true)) << "1 in queue_";

    EXPECT_TRUE(mt_getQ().setHdlrOK<int>([](SafePtr<void>){})) << "REQ: set hdlr";
    EXPECT_EQ(1u, mt_getQ().nHdlr()) << "1 hdlr";

    mt_getQ().clearAll();
    EXPECT_EQ(0u, mt_getQ().size(true)) << "REQ: clear all ele";
    EXPECT_EQ(0u, mt_getQ().nHdlr()) << "REQ: clear all hdlr";
}
TEST_F(MtInQueueTest, cov_destructor)
{
    MtInQueue mtQ;  // cov destructor with ele left
    EXPECT_TRUE(mtQ.mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";

    MtInQueue mtQ2;  // cov destructor without ele left
}

#define HDLR
// ***********************************************************************************************
// normal covered by MT_NotifierTest
TEST_F(MtInQueueTest, GOLD_handle_bothCacheAndQueue_ifPossible_withoutBlocked)
{
    // init
    size_t nCalled = 0;
    EXPECT_TRUE(mt_getQ().setHdlrOK<int>([&nCalled](SafePtr<void>){ ++nCalled; })) << "REQ: set hdlr";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    auto popped = mt_getQ().pop();  // still 1 ele in cache_
    EXPECT_NE(nullptr, popped.get()) << "REQ: pop from cache";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";  // and 1 ele in queue_
    EXPECT_EQ(2u, mt_getQ().size(true));

    {
        auto guard = mt_getQ().lockBackdoor();
        EXPECT_EQ(1u, mt_getQ().handleAllEle()) << "REQ: handle cache_, avoid blocked on queue_";
        EXPECT_EQ(1u, nCalled);
    }
    mt_getQ().handleAllEle();
    EXPECT_EQ(0u, mt_getQ().size(true)) << "REQ: shall handle queue_ since unlocked";
    EXPECT_EQ(2u, nCalled);
}
TEST_F(MtInQueueTest, handle_emptyQ)
{
    EXPECT_EQ(0u, mt_getQ().size(true)) << "REQ: can handle empty Q";
    mt_getQ().handleAllEle();
}
TEST_F(MtInQueueTest, hdlr_cannot_null_overwrite)
{
    struct HdlrSlot {};
    EXPECT_FALSE(mt_getQ().setHdlrOK<HdlrSlot>(nullptr));
    EXPECT_EQ(0, mt_getQ().nHdlr()) << "REQ: hdlr=null doesn't make sense";

    EXPECT_TRUE(mt_getQ().setHdlrOK<HdlrSlot>([](SafePtr<void>){}));
    EXPECT_EQ(1, mt_getQ().nHdlr()) << "set new hdlr";
    EXPECT_FALSE(mt_getQ().setHdlrOK<HdlrSlot>([](SafePtr<void>){})) << "REQ: can NOT overwrite hdlr";
}
TEST_F(MtInQueueTest, handle_via_base)
{
    struct Base { virtual int value() { return 1; } };
    struct Derive : public Base { int value() override { return 2; } };
    // mt_getQ().mt_pushOK<Derive>(new_safe<Base>());  // REQ: build err to push Base to Derive

    EXPECT_TRUE(mt_getQ().mt_pushOK<Base>(new_safe<Base>())) << "REQ: push OK";
    EXPECT_EQ(nullptr, mt_getQ().pop<Derive>().get()) << "REQ: pop derive type is invalid";
    EXPECT_EQ(1u, mt_getQ().size(true)) << "REQ: mismatch leaves that element";

    auto d = new_safe<Derive>();
    EXPECT_TRUE(mt_getQ().mt_pushOK<Base>(move(d))) << "REQ: push OK";
    EXPECT_EQ(2u, mt_getQ().size(true)) << "REQ: can push Derive to Base";

    int exp = 1;
    EXPECT_TRUE(mt_getQ().setHdlrOK<Base>([&exp](SafePtr<void> aEle)
    {
        auto ele = safe_cast<Base>(aEle);
        EXPECT_NE(nullptr, ele.get());
        EXPECT_EQ(exp++, ele.get()->value());
    }));
    mt_getQ().handleAllEle();
    EXPECT_EQ(0u, mt_getQ().size(true)) << "req: Base & Derive are handled correctly";
}
TEST_F(MtInQueueTest, noHdlrEle_discard)
{
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>(1))) << "REQ: push OK";
    EXPECT_EQ(1u, mt_getQ().size(true));

    mt_getQ().handleAllEle();
    EXPECT_EQ(0u, mt_getQ().size(true)) << "REQ: discard ele w/o hdlr - simple & no leak/crash";
}
TEST_F(MtInQueueTest, hdlr_except)
{
    int step = 0;
    EXPECT_TRUE(mt_getQ().setHdlrOK<int>([&step](SafePtr<void>)
    {
        ++step;
        throw runtime_error("MtQ hdlr except");
    })) << "REQ: set hdlr";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>())) << "REQ: push OK";
    EXPECT_TRUE(mt_getQ().mt_pushOK<int>(new_safe<int>())) << "REQ: push OK";
    EXPECT_EQ(2u, mt_getQ().handleAllEle()) << "REQ: handled 2 elements";
    EXPECT_EQ(2, step) << "REQ: hdlr called";
}

}  // namespace
