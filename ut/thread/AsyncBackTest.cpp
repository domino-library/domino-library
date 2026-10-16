/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define  THREAD_BACK_TYPE  AsyncBack
#define  THREAD_BACK_TEST  AsyncBackTest
#include "ThreadBackTest.hpp"
#undef   THREAD_BACK_TYPE
#undef   THREAD_BACK_TEST

namespace rlib
{
// ***********************************************************************************************
TEST_F(AsyncBackTest, GOLD_limitNewTaskOK_rejectWhenFull_then_acceptWhenFreed)
{
    std::atomic<bool> canEnd(false);
    AsyncBack myBack(2);  // capacity=2

    // fill 2 threads
    EXPECT_TRUE(myBack.limitNewTaskOK(
        [&canEnd] {
            while (not canEnd) std::this_thread::yield();
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 1st task OK";

    EXPECT_TRUE(myBack.limitNewTaskOK(
        [&canEnd] {
            while (not canEnd) std::this_thread::yield();
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 2nd task OK";

    // 3rd task: rejected (full)
    std::atomic<bool> rejectedRan{false};
    EXPECT_FALSE(myBack.limitNewTaskOK(
        [&rejectedRan] {
            rejectedRan = true;
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: rejected when full";

    // release and accept
    canEnd = true;
    while (myBack.hdlDoneFut() == 0)
        timedwait();

    EXPECT_TRUE(myBack.limitNewTaskOK(
        [] { return make_safe<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: accept after slot freed";

    drainDoneFut(myBack);
    EXPECT_FALSE(rejectedRan.load()) << "REQ: rejected task never runs";
}

// ThPoolBack has no ready-count: a completion is either on the done list or not.
// This race exists only on the future path (count +1 before future::ready).
TEST_F(AsyncBackTest, bugFix_nDoneFut_before_futureReady)
{
    std::atomic<bool> canEnd(false);
    EXPECT_TRUE(threadBack_.newTaskOK(
        [&canEnd]()
        {
            while (not canEnd)
                std::this_thread::yield();
            return make_safe<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: newTaskOK";

    threadBack_.mt_nDoneFut()++;  // force +1 before future ready
    EXPECT_EQ(0u, threadBack_.hdlDoneFut()) << "REQ: no crash, nothing ready";

    canEnd = true;
    threadBack_.waitAllFut_forUt();
    EXPECT_EQ(2u, threadBack_.mt_nDoneFut().load()) << "REQ: +1 visible once future ready";
    EXPECT_EQ(1u, threadBack_.hdlDoneFut());
    threadBack_.mt_nDoneFut()--;  // undo the forced +1; leave the real count at 0
    EXPECT_EQ(0u, threadBack_.mt_nDoneFut().load());
}

}  // namespace
