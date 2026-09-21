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

        // create new thread
        // - impossible thread alive but AsyncBack destructed, since ~AsyncBack() will wait all fut over
        // - &mt_nDoneFut is better than "this" that can access other non-MT-safe member
        fut_backFN_S_.push_back(Fut_BackFN{
            async(launch::async, &AsyncBack::mt_thMain_, std::move(mt_aEntryFN), std::ref(mt_nDoneFut_)),
            std::move(aBackFN)
        });
        return true;
    } catch(...) {  // - ut can't cover this branch
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
