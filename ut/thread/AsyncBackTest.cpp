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
    EXPECT_FALSE(myBack.limitNewTaskOK(
        [] { return make_safe<bool>(true); },
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
}

}  // namespace
