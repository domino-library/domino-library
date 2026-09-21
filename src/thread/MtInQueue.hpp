/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - what:
//   * multi-thread-safe queue - multi-thread in & main-thread out - FIFO
//   . can store any type data
//   . high performance by pop-cache
//
// - why:
//   . multi-thread msg into MtInQueue, main thread read & handle (so call "In"Queue)
//     . "Out"Queue is not clear: eg main thread can write socket directly w/o block or via ThreadBack
//   . fetch() in the middle of the queue is not common, let class simple (eg std::queue than list)
//   . cache_: if mt_pushOK() heavily, cache_ avoid ~all mutex from pop()
//
// - core:
//   . mt_queue_
//
// - class safe: true (when use SafePtr instead of shared_ptr)
//   . MtInQueue is for normal/most scenario, may NOK for high throughput, etc
//   . not limit queue size as std lib, usr responds for safety
// ***********************************************************************************************
#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <typeindex>
#include <utility>
#include <vector>

#include "MT_PingMainTH.hpp"
#include "UniLog.hpp"
#include "UniPtr.hpp"

namespace rlib
{
// - ele & its type_index(==/!= ok, but type_info* & hash_code nok)
// - shared_ptr is safe to cast void since type_index (but not safe as SafePtr's create)
using ELE_TID = std::pair<UniPtr, std::type_index>;
using EleHdlr = std::function<void(UniPtr)>;

// ***********************************************************************************************
class MtInQueue
{
public:
    ~MtInQueue() noexcept;  // noexcept like most interfaces, explicit is safer to usr than implicit!!!

    // - aEle may not mt-safe, so here mv rather than cp
    // - except eg bad_alloc: can't recover->terminate
    template<class aEleType> [[nodiscard]] bool mt_pushOK(S_PTR<aEleType>&& aEle) noexcept;

    // - shall be called in main thread ONLY!!!
    // - high performance
    [[nodiscard]] ELE_TID pop() noexcept;
    template<class aEleType> [[nodiscard]] S_PTR<aEleType> pop() noexcept;
    [[nodiscard]] size_t size(bool canBlock) const noexcept;
    void clearAll() noexcept;  // clear elements + handlers

    // shall be called in main thread ONLY!!!
    template<class aEleType> [[nodiscard]] bool setHdlrOK(EleHdlr) noexcept;  // except eg bad_alloc: can't recover->terminate
    size_t handleAllEle() noexcept;
    [[nodiscard]] auto nHdlr() const noexcept { return tid_hdlr_S_.size(); }
    void clearHdlrPool() noexcept { decltype(tid_hdlr_S_)().swap(tid_hdlr_S_); }

private:
    std::deque<ELE_TID>::iterator begin_() noexcept;
    size_t  handleCacheEle_() noexcept;

    // -------------------------------------------------------------------------------------------
    std::deque<ELE_TID> mt_queue_;  // most suitable container
    std::deque<ELE_TID> cache_;  // main-thread use ONLY (so no mutex protect)
    mutable std::mutex mt_mutex_;

    // REQ: max perf/mem
    // - full dense: all type_index need 1 hdlr, so vector is better than unordered_map
    //   . perf: cache-friendly, O(1) indexed, no hash on hot path
    //   . mem: ~40B/hdlr (unordered_map is ~72B)
    std::vector<std::pair<std::type_index, EleHdlr>> tid_hdlr_S_;

    // -------------------------------------------------------------------------------------------
#ifdef IN_GTEST
public:
    [[nodiscard]] std::unique_lock<std::mutex> lockBackdoor() { return std::unique_lock(mt_mutex_); }
#endif
};

// ***********************************************************************************************
template<class aEleType>
bool MtInQueue::mt_pushOK(S_PTR<aEleType>&& aEle) noexcept
{
    // validate aEle
    if (!aEle)
    {
        HID("!!! can't push nullptr since pop empty will ret nullptr");
        return false;
    }
    // for MT safe: mt_pushOK shall take over aEle's content (so sender can't touch the content)
    if (aEle.use_count() > 1)
    {
        HID("!!! push failed since use_count=" << aEle.use_count());  // ERR() is not MT safe
        return false;
    }

    // push
    try {
        std::lock_guard guard(mt_mutex_);
        // HID("(MtQ) nRef=" << aEle.use_count() << ", ptr=" << aEle.get());  // HID supports MT
        mt_queue_.emplace_back(std::move(aEle), typeid(aEleType));  // ELE_TID: Element+TypeId
    } catch(...) {  // ut can't cover this branch; rare but safer
        HID("!!! MtQ except in mt_pushOK");  // HID supports MT (ERR/WRN don't)
        return false;
    }

    // unlock then notify main thread
    mt_pingMainTH();
    return true;
}

// ***********************************************************************************************
template<class aEleType>
S_PTR<aEleType> MtInQueue::pop() noexcept
{
    // nothing
    auto&& ele_tid = begin_();
    if (ele_tid == cache_.end())
        return nullptr;

    // mismatch
    if (ele_tid->second != typeid(aEleType))
        return nullptr;

    // pop
    auto ele = std::move(ele_tid->first);
    cache_.pop_front();
    return STATIC_PTR_CAST<aEleType>(ele);
}

// ***********************************************************************************************
template<class aEleType>
bool MtInQueue::setHdlrOK(EleHdlr aHdlr) noexcept
{
    if (! mt_reqMainTH(__func__))
        return false;

    if (! aHdlr)
    {
        WRN("(MtQ) why set null hdlr?");
        return false;
    }

    for (auto& [tid, hdlr] : tid_hdlr_S_)
        if (tid == std::type_index(typeid(aEleType)))
        {
            ERR("(MtQ) failed!!! overwrite hdlr may unsafe existing data");
            return false;
        }
    try {
        tid_hdlr_S_.emplace_back(std::type_index(typeid(aEleType)), std::move(aHdlr));
    } catch(...) {  // ut can't cover this branch; rare but safer
        ERR("(MtQ) except=" << mt_exceptInfo() << " in setHdlrOK");
        return false;
    }
    return true;
}



// ***********************************************************************************************
// - REQ: can provide different impl w/o usr code impact
// - REQ: MT safe
MtInQueue& mt_getQ() noexcept;

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2022-02-18  CSZ       1)create
// 2022-08-18  CSZ       - replace CppLog by UniLog
// 2022-11-23  CSZ       - rm since no need
// 2023-08-23  CSZ       - re-add since potential need
// 2023-08-24  CSZ       2)MtQueue -> MtInQueue
// 2023-09-08  CSZ       - store Ele with its hash_code
// 2023-09-18  CSZ       - support main thread wait() instead of keeping pop()
// 2023-10-26  CSZ       - replace mt_notifyFn_() by mt_pingMainTH()
// 2023-10-29  CSZ       - integrate handler
// 2024-02-15  CSZ       3)use SafePtr (mem-safe); shared_ptr is not mem-safe
// 2024-03-10  CSZ       - enhance safe of setHdlrOK()
// 2024-10-05  CSZ       - integrate with domino (giveup since multi-same-type)
// 2025-03-24  CSZ       4)enable exception: tolerate except is safer; can't recover except->terminate
// 2026-04-08  CSZ       - safe to limit max queue
// ***********************************************************************************************
