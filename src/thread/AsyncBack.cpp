/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <chrono>
#include <utility>
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
        maxInFlight_ = aMaxAsync;  // construct-time limit for limitNewTaskOK
    } catch(...) {  // ut can't cover this branch; rare but safer
        ERR("(AsyncBack) Except=" << mt_exceptInfo() << " when reserving aMaxAsync=" << aMaxAsync);
        throw;
    }
}

// ***********************************************************************************************
bool AsyncBack::newTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    try {
        if (! newTaskArgOK_(mt_aEntryFN, aBackFN, oneLog))
            return false;

        // Grow first. push_back after async() must not allocate: if it threw, ~future would
        // block until the task finished, and mt_nDoneFut_ would stay one ahead with no backFN.
        // Pass &mt_nDoneFut_, not this: this would let the worker touch main-thread members.
        // ~future from std::async waits out that worker, so the thread cannot outlive this object.
        if (fut_backFN_S_.size() == fut_backFN_S_.capacity())
            fut_backFN_S_.reserve(fut_backFN_S_.size() * 2 + 1);
        fut_backFN_S_.push_back(MT_AsyncTaskInfo{
            std::move(aBackFN),
            async(launch::async, &AsyncBack::mt_thMain_, std::move(mt_aEntryFN), std::ref(mt_nDoneFut_)),
        });
        return true;
    } catch(...) {  // reserve or async() failed before a task was kept
        ERR("(AsyncBack) except=" << mt_exceptInfo() << " to create new thread!!!");
        return false;
    }
}  // newTaskOK

// ***********************************************************************************************
AsyncBack::~AsyncBack() noexcept
{
    if (const auto n = fut_backFN_S_.size())
        WRN("(AsyncBack) discard nFut=" << n);
}

// ***********************************************************************************************
size_t AsyncBack::hdlDoneFut(UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return 0;

    size_t nHandledFut = 0;
    const auto nDoneFut = mt_nDoneFut_.load(memory_order_acquire);  // ensure visibility of producer's writes
    if (nDoneFut == 0) return 0;

    // bugFix: may mt_nDoneFut_+1 before future::ready, so must check size
    for (size_t i = 0; i < fut_backFN_S_.size() && nHandledFut < nDoneFut;)
    {
        // - async() failure throws -> newTaskOK returns false
        // - a valid async() future is never invalid
        auto& fut = fut_backFN_S_[i].mt_future_;
        if (fut.wait_for(0s) == future_status::ready)
        {
            auto task = std::move(fut_backFN_S_[i]);
            // swap-erase: move last element into this slot, then pop_back
            if (i + 1 < fut_backFN_S_.size())
                fut_backFN_S_[i] = std::move(fut_backFN_S_.back());
            fut_backFN_S_.pop_back();
            ++nHandledFut;

            SafePtr ret;
            try { ret = task.mt_future_.get(); }
            catch(...) { ERR("(AsyncBack) entryFN() except=" << mt_exceptInfo()); }

            try { task.backFN_(move(ret)); }  // callback
            catch(...) { ERR("(AsyncBack) backFN() except=" << mt_exceptInfo()); }
        }
        else
            ++i;
    }  // 1 loop, simple & safe

    mt_nDoneFut_.fetch_sub(nHandledFut, memory_order_acq_rel);  // proper sync with producer threads
    return nHandledFut;
}

// ***********************************************************************************************
SafePtr<void> AsyncBack::mt_thMain_(MT_TaskEntryFN mt_aEntryFN, std::atomic<size_t>& mt_nDoneFut) noexcept
{
    SafePtr ret;
    try { ret = mt_aEntryFN(); }
    catch(...) {
        EMG("(AsyncBack) entryFN() except=%s", mt_exceptInfo());
    }
    mt_aEntryFN = nullptr;  // early release captured function

    mt_nDoneFut.fetch_add(1, std::memory_order_release);  // sync with consumer's acquire
    mt_pingMainTH();
    return ret;
}

}  // namespace
