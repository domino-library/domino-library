/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Why:
//   . alt AsyncBack by limit thread# (limited resource) but may wait more time for free thread
//   . avoid create/destroy thread, 10~100 faster than AsyncBack
//   . code more complex than AsyncBack
//
// - Use-safe: yes
// - MT safe:
//   . MT_/mt_ prefix: yes
//   . others: no (must call in main thread)
// ***********************************************************************************************
#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "ThreadBack.hpp"

namespace rlib
{
// ***********************************************************************************************
class ThPoolBack : public ThreadBack
{
public:
    // @param aMaxThread: thread pool size; 0 -> MAX_THREAD (with warning)
    // @param aMaxTaskQ: task queue capacity for limitNewTaskOK; 0 -> MAX_TASKQ (with warning)
    // @except if any thread cannot be created/joinable
    explicit ThPoolBack(size_t aMaxThread = MAX_THREAD, size_t aMaxTaskQ = MAX_TASKQ) noexcept(false);
    ~ThPoolBack() noexcept;

    ThPoolBack(const ThPoolBack&) = delete;
    ThPoolBack& operator=(const ThPoolBack&) = delete;
    ThPoolBack(ThPoolBack&&) = delete;
    ThPoolBack& operator=(ThPoolBack&&) = delete;

    [[nodiscard]] bool newTaskOK(MT_TaskEntryFN, TaskBackFN, UniLog& = UniLog::defaultUniLog_) noexcept override;
    [[nodiscard]] size_t hdlDoneFut(UniLog& = UniLog::defaultUniLog_) noexcept override;
    [[nodiscard]] size_t nFut() const noexcept override { return nInFlight_; }

private:
    void mt_threadMain_() noexcept;  // runs in each pool thread
    void clean_() noexcept;

    // -------------------------------------------------------------------------------------------
    std::vector<std::thread>  thPool_;

    // simple: no need promise/future & related headache
    struct MT_ThTaskInfo_
    {
        MT_TaskEntryFN mt_entryFN_;
        TaskBackFN backFN_;
        SafePtr<void> mt_ret_;
    };
    std::list<MT_ThTaskInfo_>  mt_taskQ_;
    std::list<MT_ThTaskInfo_>  mt_doneQ_;  // professional than counter(sync headache)

    std::mutex  mt_mutex_;
    std::condition_variable  mt_qCv_;
    size_t  nInFlight_ = 0;  // main thread only: accepted, backFN not yet run
    std::atomic<bool>  mt_stopAllTH_ = false;

    // -------------------------------------------------------------------------------------------
    static constexpr size_t  MAX_THREAD = 100;      // rational default; stack costs 100*8M=800M
    static constexpr size_t  MAX_TASKQ  = 10'000;   // rational default for limitNewTaskOK

#ifdef IN_ALL_UT
public:
    bool mt_stopping_forUt() const noexcept { return mt_stopAllTH_.load(); }
    // Main thread, outside hdlDoneFut. From a backFN only when that call holds no other
    // snapshot task: those still count in nInFlight_ but are not in mt_doneQ_, so this never returns.
    void waitAllFut_forUt()
    {
        for (;;)
        {
            {
                std::lock_guard lock(mt_mutex_);
                if (mt_doneQ_.size() == nInFlight_)
                    return;
            }
            timedwait();
        }
    }
#endif
};

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-07-09  CSZ       1)create
// 2025-03-21  CSZ       2)enable exception: tolerate except is safer; can't recover except->terminate
// 2026-04-09  CSZ       - can limit max tasks
// 2026-10-02  CSZ       - count +1 before the future is ready
//                       - entryFN except stays on the worker (tsan)
// 2026-10-13  CSZ       - enhance & unify dtor
// 2026-10-14  CSZ       - std::list + splice; node allocated on submit
// ***********************************************************************************************
