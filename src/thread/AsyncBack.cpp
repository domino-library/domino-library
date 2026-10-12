/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "AsyncBack.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
AsyncBack::AsyncBack(size_t aMaxAsync)
{
    try {
        if (aMaxAsync == 0)
        {
            WRN("!!! aMaxAsync=0 is meaningless, force to " << MAX_ASYNC);
            aMaxAsync = MAX_ASYNC;
        }
        fut_backFN_S_.reserve(aMaxAsync);  // not construct any async; better perf for most cases
        maxParallel_ = aMaxAsync;  // construct-time limit for limitNewTaskOK
    } catch(...) {  // ut can't cover this branch; rare but safer
        ERR("(AsyncBack) Except=" << mt_exceptInfo() << " when reserving aMaxAsync=" << aMaxAsync);
        throw;
    }
}

// ***********************************************************************************************
bool AsyncBack::newTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    try {
        if (! ThreadBack::newTaskOK(mt_aEntryFN, aBackFN, oneLog))
            return false;

        // Grow first. push_back after async() must not allocate: if it threw, ~future would
        // block until the task finished, and mt_nDoneFut_ would stay one ahead with no backFN.
        // Pass &mt_nDoneFut_, not this: this would let the worker touch main-thread members.
        // ~future from std::async waits out that worker, so the thread cannot outlive this object.
        if (fut_backFN_S_.size() == fut_backFN_S_.capacity())
            fut_backFN_S_.reserve(fut_backFN_S_.size() * 2 + 1);
        fut_backFN_S_.push_back(Fut_BackFN{
            async(launch::async, &AsyncBack::mt_thMain_, std::move(mt_aEntryFN), std::ref(mt_nDoneFut_)),
            std::move(aBackFN)
        });
        return true;
    } catch(...) {  // reserve or async() failed before a task was kept
        ERR("(AsyncBack) except=" << mt_exceptInfo() << " to create new thread!!!");
        return false;
    }
}  // newTaskOK

// ***********************************************************************************************
SafePtr<void> AsyncBack::mt_thMain_(MT_TaskEntryFN mt_aEntryFN, std::atomic<size_t>& mt_nDoneFut) noexcept
{
    SafePtr ret;
    try { ret = mt_aEntryFN(); }
    catch(...) { HID("(AsyncBack) entryFN() except=" << mt_exceptInfo()); }  // continue following
    mt_aEntryFN = nullptr;  // early release captured function

    mt_nDoneFut.fetch_add(1, std::memory_order_release);  // sync with consumer's acquire
    mt_pingMainTH();
    return ret;
}

}  // namespace
