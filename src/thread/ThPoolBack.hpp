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
// - Use-safe: yes with condition:
//   . not support TOO many tasks that exceeds fut_backFN_S_/mt_nDoneFut_/.. (impossible in most/normal cases)
//   . destructor will FOREVER wait all thread finished
// - MT safe:
//   . MT_/mt_ prefix: yes
//   . others: no (must call in main thread)
// ***********************************************************************************************
#pragma once

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <deque>
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

private:
    void mt_threadMain_() noexcept;  // runs in each pool thread
    void clean_() noexcept;
    // -------------------------------------------------------------------------------------------
    std::vector<std::thread>  thPool_;

    std::deque<std::packaged_task<SafePtr<void>()>>  mt_taskQ_;
    std::mutex  mt_mutex_;
    std::condition_variable  mt_qCv_;

    std::atomic<bool>  mt_stopAllTH_ = false;

    // -------------------------------------------------------------------------------------------
    static constexpr size_t  MAX_THREAD = 100;      // rational default; stack costs 100*8M=800M
    static constexpr size_t  MAX_TASKQ  = 10'000;   // rational default for limitNewTaskOK
};

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-07-09  CSZ       1)create
// 2025-03-21  CSZ       2)enable exception: tolerate except is safer; can't recover except->terminate
// 2026-04-09  CSZ       - can limit max tasks
// ***********************************************************************************************
