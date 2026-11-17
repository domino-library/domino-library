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
namespace {
std::atomic<bool> g_canEnd{false};  // worker may poll; a pointer arg is not an entry arg
std::atomic<bool> g_rejected{false};
std::atomic<bool> g_inTask{false};
ThPoolBack* g_pool = nullptr;       // dtor test watches the pool's own stop flag
}
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
                return make_over<bool>(true);
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
                return make_over<bool>(true);
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
    g_canEnd = false;
    g_rejected = false;
    ThPoolBack myPool(1, 3);  // 1 thread, capacity=3

    // 1st task: block the only thread
    EXPECT_TRUE(myPool.limitNewTaskOK(
        []() -> SafePtrOver<void> {
            while (not g_canEnd) std::this_thread::yield();
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 1st task OK (running)";

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 2nd & 3rd tasks queue up
    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_over<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: 2nd task OK";

    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_over<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: 3rd task OK";

    // 4th task: rejected (full)
    EXPECT_FALSE(myPool.limitNewTaskOK(
        []() -> SafePtrOver<void> {
            g_rejected = true;
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: rejected when full";

    // release and accept
    g_canEnd = true;
    while (myPool.hdlDoneFut() == 0)
        timedwait();

    EXPECT_TRUE(myPool.limitNewTaskOK(
        [] { return make_over<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: accept after slot freed";

    drainDoneFut(myPool);
    EXPECT_FALSE(g_rejected.load()) << "REQ: rejected task never runs";
}

// ***********************************************************************************************
// task1 returns only after stop, so the worker exits without taking the queued task2
TEST_F(ThPoolBackTest, dtor_discards_finished_and_queued_backFN)
{
    g_inTask = false;
    std::atomic<int> nBack{0};
    {
        ThPoolBack pool(1, 2);
        g_pool = &pool;
        EXPECT_TRUE(pool.newTaskOK(
            []() -> SafePtrOver<void> {
                g_inTask = true;
                while (g_pool && !g_pool->mt_stopping_forUt()) std::this_thread::yield();
                return make_over<bool>(true);
            },
            [&](SafePtr<void>) { nBack += 1; }
        ));
        while (!g_inTask) std::this_thread::yield();  // else stop may come before task1 starts
        EXPECT_TRUE(pool.newTaskOK(
            [] { return make_over<bool>(true); },
            [&](SafePtr<void>) { nBack += 10; }
        ));
    }
    g_pool = nullptr;
    EXPECT_EQ(0, nBack.load()) << "REQ: dtor drops the finished backFN and the queued one";
}

// ***********************************************************************************************
TEST_F(ThPoolBackTest, maxTaskQ_0_forced_to_default)
{
    ThPoolBack myPool(2, 0);  // 0 forced to MAX_TASKQ=10000
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(myPool.limitNewTaskOK(
            [] { return make_over<bool>(true); },
            [](SafePtr<void>) {}
        )) << "REQ: 0 forced to default";
    }
    drainDoneFut(myPool);
}

TEST_F(ThPoolBackTest, bugFix_hugeCap_throws)
{
    ASSERT_GT(size_t(-1), (std::vector<std::thread>{}.max_size()));
    EXPECT_THROW((ThPoolBack{size_t(-1), 1}), std::length_error);
}

}  // namespace
