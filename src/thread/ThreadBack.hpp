/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Why this class:
//   * common interface so can replace diff solutions eg AsyncBack or ThPoolBack
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

#include <functional>
#include <type_traits>
#include <utility>

#include "MT_SafeThEntryFN.hpp"
#include "UniLog.hpp"

namespace rlib
{
// ***********************************************************************************************
using UsrBackFN = std::function<void(SafePtr<void>)>;  // main thread: the cross is over

// ***********************************************************************************************
class ThreadBack
{
public:
    // min for safety:
    // - discard backFN(): avoid longer & more complex scenario
    // - wait workers FOREVER: safe (& no choice)
    ThreadBack() noexcept = default;
    virtual ~ThreadBack() noexcept = default;
    // forbid:
    ThreadBack(const ThreadBack&) = delete;
    ThreadBack& operator=(const ThreadBack&) = delete;
    ThreadBack(ThreadBack&&) = delete;
    ThreadBack& operator=(ThreadBack&&) = delete;

    // - aUsrEntryFN: like std::thread(mt_aFN, mt_aArgPack...)
    // - UsrBackFN: invoked on the main thread
    template<class aUsrEntryFN, class... aEntryArgPack>
    [[nodiscard]] bool newTaskOK(aUsrEntryFN, UsrBackFN, aEntryArgPack&&...) noexcept;

    // @brief: like newTaskOK but reject if nFut() >= maxInFlight_ (for users who want to limit max#)
    template<class aUsrEntryFN, class... aEntryArgPack>
    [[nodiscard]] bool limitNewTaskOK(aUsrEntryFN, UsrBackFN, aEntryArgPack&&...) noexcept;

    // @brief: Processes completed tasks and invokes their callbacks on the main thread.
    // @param UniLog: The logger to use for this operation.
    // @return: The number of completed tasks processed.
    [[nodiscard]] virtual size_t hdlDoneFut(UniLog& = UniLog::defaultUniLog_) noexcept = 0;

    [[nodiscard]] virtual size_t nFut() const noexcept = 0;

private:
    // -------------------------------------------------------------------------------------------
    // derived classes enqueue here. Users call the template newTaskOK above.
    [[nodiscard]] virtual bool newTaskOK_(MT_SafeThEntryFN, UsrBackFN, UniLog& = UniLog::defaultUniLog_) noexcept = 0;
protected:
    [[nodiscard]] bool newTaskArgOK_(const MT_SafeThEntryFN&, const UsrBackFN&, UniLog&) noexcept;

    // limitNewTaskOK cap on outstanding tasks (nFut). Not a thread count:
    // AsyncBack: max async tasks. ThPoolBack: max task-queue depth.
    // newTaskOK itself may exceed it.
    size_t  maxInFlight_ = 0;
};

// ***********************************************************************************************
template<class aUsrEntryFN, class... aEntryArgPack>
bool ThreadBack::limitNewTaskOK(aUsrEntryFN mt_aFN, UsrBackFN aBackFN, aEntryArgPack&&... mt_aArgPack) noexcept
{
    if (!mt_reqMainTH(__func__))
        return false;
    if (nFut() >= maxInFlight_)
    {
        WRN("(ThreadBack) max reached (max=" << maxInFlight_ << "), reject new task");
        return false;
    }
    return newTaskOK(mt_aFN, std::move(aBackFN), std::forward<aEntryArgPack>(mt_aArgPack)...);
}

template<class aUsrEntryFN, class... aEntryArgPack>
bool ThreadBack::newTaskOK(aUsrEntryFN mt_aFN, UsrBackFN aBackFN, aEntryArgPack&&... mt_aArgPack) noexcept
{
    static_assert(is_entry_fn<aUsrEntryFN>::value, "(ThreadBack) MT task must be capture-less");
    if (!mt_reqMainTH(__func__))
        return false;
    if (!aBackFN)
    {
        ERR("(ThreadBack) aBackFN=null doesn't make sense!!! Why not async() directly?");
        return false;
    }
    MT_SafeThEntryFN entry(mt_aFN, std::forward<aEntryArgPack>(mt_aArgPack)...);
    if (!entry)  return false;
    return newTaskOK_(std::move(entry), std::move(aBackFN), UniLog::defaultUniLog_);
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-07-09  CSZ       1)create
// 2024-08-05  CSZ       - mt_nDoneFut_ to improve iteration of fut_backFN_S_
//                       - MT_TaskEntryFN ret SafePtr<void> instead of bool
// 2025-03-21  CSZ       2)enable exception: tolerate except is safer; can't recover except->terminate
// 2026-10-13  CSZ       - enhance & unify dtor
// 2026-10-14  CSZ       3)refactor since professional ThPoolBack
// 2026-10-19  CSZ       - delete copy/move (polymorphic base)
// 2026-11-05  CSZ       4)enhance entry-fn safety
// 2026-11-13  CSZ       - backFN takes SafePtr: thread already crossed
// ***********************************************************************************************
// - why SafePtrOver
//   . entry args and the entry return stay SafePtrOver: one owner across the thread
//   . backFN is already on the main thread: SafePtr<void>(std::move(over))
