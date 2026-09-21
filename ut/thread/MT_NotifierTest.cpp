/**
 * Copyright 2023 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - basic notify/wait/timeout/dedup already covered via ThreadBackTest, MtInQueueTest, MsgSelfTest
// ***********************************************************************************************
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <time.h>

#include "UniLog.hpp"

#define IN_GTEST
#include "MT_Notifier.hpp"
#include "MT_PingMainTH.hpp"
#undef IN_GTEST

using namespace std;
using namespace testing;

namespace rlib
{
// ***********************************************************************************************
struct MT_NotifierTest : public Test, public UniLog
{
    MT_NotifierTest()
        : UniLog(UnitTest::GetInstance()->current_test_info()->name())
    { mt_getMainTH(); }  // designate this (gtest) thread as the logical main
    ~MT_NotifierTest()
    {
        GTEST_LOG_FAIL
    }

    MT_Notifier notif_;
};

// ***********************************************************************************************
TEST_F(MT_NotifierTest, large_nsec_no_crash)
{
    notif_.mt_notify();  // ensure immediate return
    notif_.timedwait(0, 2'000'000'000);  // REQ: aRestNsec >= 1s -> no crash
}

TEST_F(MT_NotifierTest, max_nsec_no_crash)
{
    notif_.mt_notify();
    notif_.timedwait(0, size_t(-1));  // REQ: extreme ns -> no crash (same as ThreadBackTest::timeout)
}

// ***********************************************************************************************
TEST_F(MT_NotifierTest, timedwait_on_main_thread_ok)
{
    notif_.mt_notify();
    notif_.timedwait(0, 10'000'000);
}

TEST_F(MT_NotifierTest, timedwait_wrongThread_rejected)
{
    const auto elapsed = std::async(std::launch::async, [&]()
    {
        const auto start = chrono::steady_clock::now();
        notif_.timedwait(1, 0);
        return chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start);
    }).get();

    EXPECT_LT(elapsed.count(), 100)
        << "REQ: reject wrong-thread timedwait() instead of waiting";
}

// ***********************************************************************************************
TEST_F(MT_NotifierTest, timedwait_immune_to_clock_backward_jump)
{
    timespec probe;
    clock_gettime(CLOCK_REALTIME, &probe);
    if (clock_settime(CLOCK_REALTIME, &probe) != 0)
        GTEST_SKIP() << "can't set CLOCK_REALTIME backward";

    thread th([]{
        this_thread::sleep_for(chrono::milliseconds(50));
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec -= 3;  // backward 3s
        clock_settime(CLOCK_REALTIME, &ts);
    });

    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    notif_.timedwait(0, 300'000'000);  // 300ms
    clock_gettime(CLOCK_MONOTONIC, &t1);
    auto msDur = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1'000'000;
    // CLOCK_MONOTONIC：msDur=~300ms; if CLOCK_REALTIME：msDur=~3300ms
    EXPECT_GE(msDur, 200);
    EXPECT_LE(msDur, 1000);

    // restore
    th.join();

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    clock_settime(CLOCK_REALTIME, &ts);
}

TEST_F(MT_NotifierTest, timedwait_immune_to_clock_forward_jump)
{
    timespec probe;
    clock_gettime(CLOCK_REALTIME, &probe);
    if (clock_settime(CLOCK_REALTIME, &probe) != 0)
        GTEST_SKIP() << "can't set CLOCK_REALTIME forward";

    thread th([]{
        this_thread::sleep_for(chrono::milliseconds(50));
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;  // forward 3s
        clock_settime(CLOCK_REALTIME, &ts);
    });

    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    notif_.timedwait(0, 300'000'000);  // 300ms
    clock_gettime(CLOCK_MONOTONIC, &t1);
    // CLOCK_MONOTONIC：msDur=~300ms; if CLOCK_REALTIME：msDur=~50ms
    auto msDur = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1'000'000;
    EXPECT_GE(msDur, 200);
    EXPECT_LE(msDur, 1000);

    // restore
    th.join();

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec -= 3;
    clock_settime(CLOCK_REALTIME, &ts);
}

// ***********************************************************************************************
TEST_F(MT_NotifierTest, GOLD_noLostNotify)
{
    notif_.reset();
    atomic<bool> stop{false};

    thread worker([&]{
        while (!stop.load(memory_order_relaxed))
            notif_.mt_notify();  // extremely surge notif
    });

    int nLost = 0;
    for (int i = 0; i < 100; ++i)
    {
        auto t0 = chrono::steady_clock::now();
        notif_.timedwait(0, 50'000'000);  // wait, or 50ms timeout
        auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
        if (ms >= 45)  // should wake immediately
            ++nLost;
    }

    stop = true;
    worker.join();
    EXPECT_EQ(0, nLost) << "REQ: no notify lost under surge notify";
}

TEST_F(MT_NotifierTest, GOLD_noImmediateNextWakeup)
{
    notif_.mt_notify();  // job1
    notif_.mt_notify();  // job2
    notif_.mt_notify();  // job3

    auto t0 = chrono::steady_clock::now();
    notif_.timedwait(0, 50'000'000);
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    EXPECT_LT(ms, 10) << "REQ: 1st timedwait should wake immediately & handle all jobs";

    t0 = chrono::steady_clock::now();
    notif_.timedwait(0, 50'000'000);  // immediate 2nd timedwait
    ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    EXPECT_GE(ms, 45) << "REQ: immediate 2nd wakeup doesn't make sense";
}

}  // namespace
