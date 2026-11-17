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
namespace {
std::atomic<bool> g_canEnd{false};      // worker may poll; a pointer arg is not an entry arg
std::atomic<bool> g_rejected{false};
}
// ***********************************************************************************************
TEST_F(AsyncBackTest, GOLD_limitNewTaskOK_rejectWhenFull_then_acceptWhenFreed)
{
    g_canEnd = false;
    g_rejected = false;
    AsyncBack myBack(2);  // capacity=2

    // fill 2 threads
    EXPECT_TRUE(myBack.limitNewTaskOK(
        []() -> SafePtrOver<void> {
            while (not g_canEnd) std::this_thread::yield();
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 1st task OK";

    EXPECT_TRUE(myBack.limitNewTaskOK(
        []() -> SafePtrOver<void> {
            while (not g_canEnd) std::this_thread::yield();
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: 2nd task OK";

    // 3rd task: rejected (full)
    EXPECT_FALSE(myBack.limitNewTaskOK(
        []() -> SafePtrOver<void> {
            g_rejected = true;
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: rejected when full";

    // release and accept
    g_canEnd = true;
    while (myBack.hdlDoneFut() == 0)
        timedwait();

    EXPECT_TRUE(myBack.limitNewTaskOK(
        [] { return make_over<bool>(true); },
        [](SafePtr<void>) {}
    )) << "REQ: accept after slot freed";

    drainDoneFut(myBack);
    EXPECT_FALSE(g_rejected.load()) << "REQ: rejected task never runs";
}

// ThPoolBack has no ready-count: a completion is either on the done list or not.
// This race exists only on the future path (count +1 before future::ready).
TEST_F(AsyncBackTest, bugFix_nDoneFut_before_futureReady)
{
    g_canEnd = false;
    EXPECT_TRUE(threadBack_.newTaskOK(
        []() -> SafePtrOver<void>
        {
            while (not g_canEnd)
                std::this_thread::yield();
            return make_over<bool>(true);
        },
        [](SafePtr<void>) {}
    )) << "REQ: newTaskOK";

    threadBack_.mt_nDoneFut()++;  // force +1 before future ready
    EXPECT_EQ(0u, threadBack_.hdlDoneFut()) << "REQ: no crash, nothing ready";

    g_canEnd = true;
    threadBack_.waitAllFut_forUt();
    EXPECT_EQ(2u, threadBack_.mt_nDoneFut().load()) << "REQ: +1 visible once future ready";
    EXPECT_EQ(1u, threadBack_.hdlDoneFut());
    threadBack_.mt_nDoneFut()--;  // undo the forced +1; leave the real count at 0
    EXPECT_EQ(0u, threadBack_.mt_nDoneFut().load());
}

TEST_F(AsyncBackTest, bugFix_hugeCap_throws)
{
    ASSERT_GT(size_t(-1), (std::vector<MT_AsyncTaskInfo>{}.max_size()));
    EXPECT_THROW(AsyncBack{size_t(-1)}, std::length_error);
}

}  // namespace
