/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - ISSUE/what:
//   . How to coordinate main thread(containing all logic) with other threads(time-consuming tasks)
//
// - Usage/how:
//                    [main thread]
//                         |
//                         | std::async()    [new thread]
//  AsyncBack::newTaskOK() |--------------------->| MT_TaskEntryFN()  // time-consuming task eg block to rec msg
//                         |                      |
//            TaskBackFN() |<.....................| (over)
//                         |
//                         |
//
// - core: TaskBackFN
//   . after MT_TaskEntryFN() end in "other thread", TaskBackFN() is auto-run in MAIN THREAD - key diff async()
//
// - VALUE/why:
//   * keep all logic (most code) in main thread (single thread: simple logic, no lock/deadlock, etc)
//     . while time-consuming tasks in other threads (min MT coding)
//   . cross platform:
//     . std::async
//     . std::future (to msg from other thread back to main thread)
//
// - REQ:
//   * run MT_TaskEntryFN() in new thread by async() (common in ThreadBack, async here)
//   * when MT_TaskEntryFN() finished, auto rouse main thread to run TaskBackFN()
//   . MT_TaskEntryFN() ret T(succ)/F(fail)
//     . this ret is as input para of TaskBackFN(); if eg async() fail, this para=F also
//   * AsyncBack is for normal/most scenario, may NOK for huge threads, high throughput, etc
//   * ONLY call AsyncBack in main-thread
//
// - Use-safe: yes with condition:
//   . not support TOO many threads (used-up thread resource; impossible in most/normal cases)
//     . not limit usr to create more async than MAX_ASYNC, but usr cover the safety (like std lib)
//   . lower performance than eg thread pool (but simpler impl than thread pool)
//   . destructor will FOREVER wait all thread finished
//   . no duty to any unsafe behavior of MT_TaskEntryFN & TaskBackFN
// - MT safe:
//   . MT_/mt_ prefix: yes
//   . others: NO (only in main thread - most dom lib code shall be in main thread - simple)
//     . mt_getMainTH() for user debug - any main-thread func: mt_getMainTH() == std::this_thread::get_id()
// ***********************************************************************************************
#pragma once

#include "ThreadBack.hpp"

namespace rlib
{
// ***********************************************************************************************
class AsyncBack : public ThreadBack
{
public:
    // @param aMaxAsync: max threads; 0 -> MAX_ASYNC (warning)
    explicit AsyncBack(size_t aMaxAsync = MAX_ASYNC) noexcept(false);
    // destruct-future will block till thread done (ut verified) - so default ~AsyncBack() is safe

    [[nodiscard]] bool newTaskOK(MT_TaskEntryFN, TaskBackFN, UniLog& = UniLog::defaultUniLog_) noexcept override;

private:
    static SafePtr<void> mt_thMain_(MT_TaskEntryFN, std::atomic<size_t>&) noexcept;  // runs in new thread
    static constexpr size_t  MAX_ASYNC = 100;  // rational default; stack costs 100*8M=800M
};

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2022-10-23  CSZ       1)create
// 2022-11-15  CSZ       - simpler & MT safe
// 2023-07-12  CSZ       - copilot compare
// 2023-10-25  CSZ       - can mt_pingMainTH() at the end of a thread
// 2023-08-17  CSZ       - handle async() fail
// 2023-08-18  CSZ       - rm mt_nFinishedThread_
// 2023-09-14  CSZ       2)align with MsgSelf
// 2023-10-25  CSZ       - with semaphore's wait-notify
// 2024-07-10  CSZ       - mv common to base=ThreadBack
// 2025-03-21  CSZ       3)enable exception: tolerate except is safer; can't recover except->terminate
// 2026-04-09  CSZ       - can limit max async
// ***********************************************************************************************
// - Q&A:
//   . MT_/mt_ prefix
//     . MT = multi-thread: mark it to run in diff thread
//     . most func/variable are in main-thread, so w/o this prefix
//   . must save future<>
//     . otherwise the thread looks like serialized with main thread
//   . limit thread#?
//     . eg linsee's /proc/sys/kernel/threads-max=154w, cloud=35w
//       . /proc/sys/kernel/threads-max is for each process
//       . so no necessary to limit
//     . most code in main-thread -> very little (eg 1%) in ThreadBack, shouldn't be many threads
//   . cancel thread?
//     . async not support cancel
//     . usr-self can cancel by eg flag
//   . thread pool?
//     . then eg future.wait_for() can't be used, may high-risk to self-impl
//     * thread pool costs more time than async (eg 0.3s:0.1s of 100 threads on belinb03)
