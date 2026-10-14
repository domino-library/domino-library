/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define  THREAD_BACK_TYPE  ThPoolBack
#define  THREAD_BACK_TEST  ThPoolBackTest
#include "ThreadBackTest.hpp"
#undef   THREAD_BACK_TYPE
#undef   THREAD_BACK_TEST

using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;

namespace rlib
{
// ***********************************************************************************************
TEST_F(ThPoolBackTest, performance)
{
    const size_t maxThread = 100;  // github ci can afford 100 (1000+ slower than belinb03)

    ThPoolBack thPoolBack(maxThread);
    auto start = high_resolution_clock::now();
    for (size_t i = 0; i < maxThread; i++)
        EXPECT_TRUE(thPoolBack.newTaskOK(
            []  // entryFn
            {
                std::this_thread::yield();  // hung like real time-cost task
                return make_safe<bool>(true);
            },
            [](SafePtr<void>) {}  // backFn
        ));
    for (size_t nHandled = 0; nHandled < maxThread; nHandled += thPoolBack.hdlDoneFut())
        timedwait();
    auto dur = duration_cast<microseconds>(high_resolution_clock::now() - start);
    HID("ThPoolBack cost=" << dur.count() << "us");
    // - belinb03 :  8~ 20 faster than AsyncBack
    // - github ci: 40~100 slower than AsyncBack

    AsyncBack asyncBack(maxThread);
    start = high_resolution_clock::now();
    for (size_t i = 0; i < maxThread; i++)
        EXPECT_TRUE(asyncBack.newTaskOK(
            []  // entryFn
            {
                std::this_thread::yield();  // hung like real time-cost task
                return make_safe<bool>(true);
            },
            [](SafePtr<void>) {}  // backFn
        ));
    for (size_t nHandled = 0; nHandled < maxThread; nHandled += asyncBack.hdlDoneFut())
        timedwait();
    dur = duration_cast<microseconds>(high_resolution_clock::now() - start);
    HID("AsyncBack cost=" << dur.count() << "us");
}

// ***********************************************************************************************
TEST_F(ThPoolBackTest, GOLD_limitNewTaskOK_rejectWhenFull_then_acceptWhenFreed)
{
    std::atomic<bool> canEnd(false);
    ThPoolBack myPool(1, 3);  // 1 thread, capacity=3

    // 1st task: block the only thread
    EXPECT_TRUE(myPool.limitNewTaskOK(
        [&canEnd] {
            while (not canEnd) std::this_thread::yield();
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 1st task OK (running)";

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 2nd & 3rd tasks queue up
    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: 2nd task OK";

    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: 3rd task OK";

    // 4th task: rejected (full)
    std::atomic<bool> rejectedRan{false};
    EXPECT_FALSE(myPool.limitNewTaskOK(
        [&rejectedRan] {
            rejectedRan = true;
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: rejected when full";

    // release and accept
    canEnd = true;
    while (myPool.hdlDoneFut() == 0)
        timedwait();

    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: accept after slot freed";

    drainDoneFut(myPool);
    EXPECT_FALSE(rejectedRan.load()) << "REQ: rejected task never runs";
}

// ***********************************************************************************************
// The queue refuses the job after the future is recorded. Without dropping that future,
// ~promise readies it with broken_promise and the next completion runs this backFN.
TEST_F(ThPoolBackTest, queuePushFail_leavesNoFuture)
{
    struct ClearFail { ~ClearFail() { ThPoolBack::failNextQueuePush_forUt = false; } } clearFail;
    ThPoolBack pool(1, 4);
    int nBack = 0;
    ThPoolBack::failNextQueuePush_forUt = true;
    EXPECT_FALSE(pool.newTaskOK(
        [] { return make_safe<bool>(true); },
        [&](SafePtr<void>) { ++nBack; }
    )) << "REQ: queue push failure rejects the task";
    ASSERT_EQ(0u, pool.nFut()) << "REQ: rejected task leaves no future";

    EXPECT_TRUE(pool.newTaskOK(
        [] { return make_safe<bool>(true); },
        [&](SafePtr<void>) { nBack += 10; }
    ));
    while (pool.hdlDoneFut() == 0)
        timedwait();
    EXPECT_EQ(10, nBack) << "REQ: the failed task's backFN does not spend the next completion";
    EXPECT_EQ(0u, pool.nFut());
}

// ***********************************************************************************************
// task1 returns only after stop, so the worker exits without taking the queued task2
TEST_F(ThPoolBackTest, dtor_discards_finished_and_queued_backFN)
{
    std::atomic<bool> inTask{false};
    std::atomic<int> nBack{0};
    {
        ThPoolBack pool(1, 2);
        EXPECT_TRUE(pool.newTaskOK(
            [&] {
                inTask = true;
                while (!pool.mt_stopping_forUt()) std::this_thread::yield();
                return make_safe<bool>(true);
            },
            [&](SafePtr<void>) { nBack += 1; }
        ));
        while (!inTask) std::this_thread::yield();  // else stop may come before task1 starts
        EXPECT_TRUE(pool.newTaskOK(
            [] { return make_safe<bool>(true); },
            [&](SafePtr<void>) { nBack += 10; }
        ));
    }
    EXPECT_EQ(0, nBack.load()) << "REQ: dtor drops the finished backFN and the queued one";
}

// ***********************************************************************************************
TEST_F(ThPoolBackTest, maxTaskQ_0_forced_to_default)
{
    ThPoolBack myPool(2, 0);  // 0 forced to MAX_TASKQ=10000
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(myPool.limitNewTaskOK(
            [] { return make_safe<bool>(true); },
            [](SafePtr<void>) {}
        )) << "REQ: 0 forced to default";
    }
    drainDoneFut(myPool);
}

}  // namespace
