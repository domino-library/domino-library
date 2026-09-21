/**
 * Copyright 2024 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ThreadBack.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
size_t ThreadBack::hdlDoneFut(UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return 0;

    size_t nHandledFut = 0;
    const auto nDoneFut = mt_nDoneFut_.load(memory_order_acquire);  // ensure visibility of producer's writes
    if (nDoneFut == 0) return 0;
    // HID("(ThreadBack) nHandled=" << nHandledFut << '/' << nDoneFut << '|' << nFut());

    // bugFix: may mt_nDoneFut_+1 before future::ready, so must check size
    for (size_t i = 0; i < fut_backFN_S_.size() && nHandledFut < nDoneFut;)
    {
        // - async() failure will throw exception -> terminate since compiling forbid exception
        // - valid async()'s future never invalid
        // - valid packaged_task's get_future() never invalid
        auto& fut = fut_backFN_S_[i].future;
        if (fut.wait_for(0s) == future_status::ready)
        {
            auto task = std::move(fut_backFN_S_[i]);
            // swap-erase: move last element into this slot, then pop_back
            if (i + 1 < fut_backFN_S_.size())
                fut_backFN_S_[i] = std::move(fut_backFN_S_.back());
            fut_backFN_S_.pop_back();
            ++nHandledFut;

            SafePtr ret;
            try { ret = task.future.get(); }
            catch(...) { ERR("(ThreadBack) entryFN() except=" << mt_exceptInfo()); }

            try { task.backFN(move(ret)); }  // callback
            catch(...) { ERR("(ThreadBack) backFN() except=" << mt_exceptInfo()); }
        }
        else
            ++i;
    }  // 1 loop, simple & safe

    mt_nDoneFut_.fetch_sub(nHandledFut, memory_order_acq_rel);  // proper sync with producer threads
    return nHandledFut;
}

// ***********************************************************************************************
bool ThreadBack::limitNewTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return false;

    if (nFut() >= maxParallel_)
    {
        WRN("(ThreadBack) max reached (max=" << maxParallel_ << "), reject new task");
        return false;
    }
    return newTaskOK(std::move(mt_aEntryFN), std::move(aBackFN), oneLog);
}

// ***********************************************************************************************
bool ThreadBack::newTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return false;

    if (! aBackFN)
    {
        ERR("(ThreadBack) aBackFN=null doesn't make sense!!! Why not async() directly?");
        return false;
    }
    if (! mt_aEntryFN)
    {
        ERR("(ThreadBack) NOT support mt_aEntryFN=null!!! Necessary?");
        return false;
    }
    return true;
}

}  // namespace
