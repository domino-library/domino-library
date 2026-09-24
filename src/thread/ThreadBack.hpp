/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Why this class:
//   . support both async() & thread pool
//   . common here, special in AsyncBack or ThPoolBack
//
// - Scenario (ThPoolBack + AsyncBack cover 92%):
//   | Scenario          | Prob | Best         | Also OK      |
//   |-------------------|------|--------------|--------------|
//   | General server    | 35%  | ThPoolBack** | bounded-Q    |
//   | IO-bound          | 25%  | ThPoolBack** | AsyncBack*   |
//   | Low-freq backend  | 20%  | AsyncBack*   | ThPoolBack** |
//   | CPU-bound         | 10%  | work-steal   | ThPoolBack** |
//   | Low-latency       |  5%  | NUMA-affin   | lock-free    |
//   | Ultra-throughput  |  3%  | lock-free    | work-steal   |
//   | Embedded          |  2%  | ThPoolBack** | bounded-Q    |
//   |-------------------|------|--------------|--------------|
//
// - Score:
//   | Sum | Method           | Correct | ImpCost | ClkImm | Ctrl | Fair | Thrupt | Latency | Resource |
//   |     | (weight)         | 25%     | 20%     | 15%    | 10%  | 10%  | 8%     | 7%      | 5%       |
//   |-----|------------------|---------|---------|--------|------|------|--------|---------|----------|
//   | 89  | sema(glibc2.30+) | 92      | 90      | 90     | 84   | 100  | 85     | 82      | 80       |
//   | 87  | ThPoolBack**(cv) | 90      | 84      | 88     | 86   | 100  | 80     | 76      | 74       |
//   | 82  | sema(<2.30)      | 92      | 90      | 40     | 84   | 100  | 85     | 82      | 80       |
//   | 80  | bounded-Q        | 86      | 74      | 85     | 88   | 76   | 72     | 70      | 82       |
//   | 75  | dynamic-pool     | 78      | 68      | 84     | 80   | 68   | 75     | 71      | 66       |
//   | 75  | priority-queue   | 79      | 64      | 84     | 92   | 62   | 70     | 72      | 70       |
//   | 74  | work-steal       | 74      | 60      | 84     | 62   | 100  | 80     | 66      | 72       |
//   | 74  | AsyncBack*       | 76      | 72      | 86     | 70   | 80   | 60     | 68      | 58       |
//   | 63  | lock-free        | 62      | 38      | 90     | 42   | 54   | 83     | 82      | 88       |
// ***********************************************************************************************
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <utility>
#include <vector>

#include "MT_PingMainTH.hpp"
#include "SafePtr.hpp"
#include "UniLog.hpp"

#define THREAD_BACK  (rlib::ObjAnywhere::getObj<rlib::ThreadBack>())

namespace rlib
{
// ***********************************************************************************************
using MT_TaskEntryFN  = std::function<SafePtr<void>()>;  // ret-nullptr means failure
using TaskBackFN      = std::function<void(SafePtr<void>)>;  // MT_TaskEntryFN's ret as para

struct Fut_BackFN {
    std::future<SafePtr<void>> future;
    TaskBackFN backFN;
};
using Fut_BackFN_S = std::vector<Fut_BackFN>;  // vector + swap-erase: O(1) rm, better cache

// ***********************************************************************************************
class ThreadBack
{
public:
    virtual ~ThreadBack() noexcept = default;

    // @brief: Schedules an asynchronous task to be executed in a background thread.
    // @param MT_TaskEntryFN: The task to be executed asynchronously.
    // @param TaskBackFN    : The callback to be invoked in the main thread upon task completion.
    // @param UniLog        : The logger to use for this operation.
    [[nodiscard]] virtual bool newTaskOK(MT_TaskEntryFN, TaskBackFN, UniLog& = UniLog::defaultUniLog_) noexcept = 0;

    // @brief: like newTaskOK but reject if nFut() >= maxParallel_ (for users who want to limit max#)
    [[nodiscard]] bool limitNewTaskOK(MT_TaskEntryFN, TaskBackFN, UniLog& = UniLog::defaultUniLog_) noexcept;

    // @brief: Processes completed threads and invokes their callbacks.
    // @param UniLog: The logger to use for this operation.
    // @return: The number of completed tasks processed.
    [[nodiscard]] size_t hdlDoneFut(UniLog& = UniLog::defaultUniLog_) noexcept;

    [[nodiscard]] auto nFut() const noexcept { return fut_backFN_S_.size(); }

protected:
    // -------------------------------------------------------------------------------------------
    // mt_nDoneFut_ must be declared BEFORE fut_backFN_S_ so it is destroyed AFTER futures
    // (async thread may call mt_nDoneFut_.fetch_add() while future destructor blocks)
    std::atomic<size_t>  mt_nDoneFut_ = 0;  // improve main thread to search done thread(s)
    Fut_BackFN_S fut_backFN_S_;  // must save future till thread end
    size_t  maxParallel_ = 0;  // can't rely on fut_backFN_S_.capacity() newTaskOK() can exceed it

    // -------------------------------------------------------------------------------------------
#ifdef IN_ALL_UT
public:
    auto& mt_nDoneFut() { return mt_nDoneFut_; }
    void waitAllFut_forUt()
    {
        for (auto& fut_backFN : fut_backFN_S_)
            fut_backFN.future.wait();
    }
#endif
};

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-07-09  CSZ       1)create
// 2024-08-05  CSZ       - mt_nDoneFut_ to improve iteration of fut_backFN_S_
//                       - MT_TaskEntryFN ret SafePtr<void> instead of bool
// 2025-03-21  CSZ       2)enable exception: tolerate except is safer; can't recover except->terminate
// ***********************************************************************************************
// - why SafePtr
//   . thread can ret any type data include bool
//   . simpler than using MtQ to ship rsp back to main thread
//   . safe convert from SafePtr<void> to corresponding data
