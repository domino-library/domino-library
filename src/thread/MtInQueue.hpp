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
// - class safe: true
//   . MtInQueue is for normal/most scenario, may NOK for high throughput, etc
//   . not limit queue size as std lib, usr responds for safety
// ***********************************************************************************************
#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <typeindex>
#include <vector>

#include "MT_PingMainTH.hpp"
#include "SafePtr.hpp"  // always SafePtr, shared_ptr can't cut a weak ref

namespace rlib
{
// - type key is SafePtr<void>::lastType(), which mt_pushOK sets from aEleType
// - aEleType can't be void: lastType would be the pre-void type, not typeid(void)
using EleHdlr = std::function<void(SafePtr<void>)>;

// ***********************************************************************************************
class MtInQueue
{
public:
    ~MtInQueue() noexcept;  // noexcept like most interfaces, explicit is safer to usr than implicit!!!

    // - only a sole new_safe: handover(), so a weak ref taken before push can't reach the ele
    // - except eg bad_alloc: can't recover->terminate
    template<class aEleType> [[nodiscard]] bool mt_pushOK(SafePtr<aEleType>&& aEle) noexcept;

    // - shall be called in main thread ONLY!!!
    // - high performance
    [[nodiscard]] SafePtr<void> pop() noexcept;
    // mismatch: leave that element and return nullptr - usr can retry than drop valuable input
    template<class aEleType> [[nodiscard]] SafePtr<aEleType> pop() noexcept;
    [[nodiscard]] size_t size(bool canBlock) const noexcept;
    void clearAll() noexcept;  // clear elements + handlers

    // shall be called in main thread ONLY!!!
    template<class aEleType> [[nodiscard]] bool setHdlrOK(EleHdlr) noexcept;  // except eg bad_alloc: can't recover->terminate
    size_t handleAllEle() noexcept;
    [[nodiscard]] auto nHdlr() const noexcept { return tid_hdlr_S_.size(); }
    void clearHdlrPool() noexcept { decltype(tid_hdlr_S_)().swap(tid_hdlr_S_); }

private:
    std::deque<SafePtr<void>>::iterator begin_() noexcept;
    size_t  handleCacheEle_() noexcept;

    // -------------------------------------------------------------------------------------------
    std::deque<SafePtr<void>> mt_queue_;  // most suitable container
    std::deque<SafePtr<void>> cache_;  // main-thread use ONLY (so no mutex protect)
    mutable std::mutex mt_mutex_;

    // REQ: max perf/mem
    // - full dense: all type_index need 1 hdlr, so vector is better than unordered_map
    //   . perf: cache-friendly, O(1) indexed, no hash on hot path
    //   . mem: ~40B/hdlr (unordered_map is ~72B)
    std::vector<std::pair<std::type_index, EleHdlr>> tid_hdlr_S_;

    // -------------------------------------------------------------------------------------------
#ifdef IN_ALL_UT
public:
    [[nodiscard]] std::unique_lock<std::mutex> lockBackdoor() { return std::unique_lock(mt_mutex_); }
#endif
};

// ***********************************************************************************************
template<class aEleType>
bool MtInQueue::mt_pushOK(SafePtr<aEleType>&& aEle) noexcept
{
    static_assert(!std::is_void_v<aEleType>,
        "(MtQ) aEleType can't be void: its lastType is the type before erasure");
    if (!aEle)
    {
        HID("!!! can't push nullptr since pop empty will ret nullptr");
        return false;
    }
    // for MT safe: mt_pushOK shall take over aEle's content (so sender can't touch the content)
    // - sender holds SafeWeak/weak_ptr: safe
    // - not 100% MT safe: eg usr holds raw_ptr, aEle.member-ptr is unsafe
    auto reHomeEle = aEle.handover();
    if (!reHomeEle)
    {
        HID("!!! push failed: not new_safe or multi-owner (use_count=" << aEle.use_count() << ")");
        return false;
    }

    // push
    try {
        std::lock_guard guard(mt_mutex_);
        mt_queue_.emplace_back(std::move(reHomeEle));  // SafePtr<void> keeps typeid(aEleType) in lastType_
    } catch(...) {  // ut can't cover this branch; rare but safer
        EMG("(MtQ) except=%s in mt_pushOK", mt_exceptInfo());
        return false;
    }

    // unlock then notify main thread
    mt_pingMainTH();
    return true;
}

// ***********************************************************************************************
template<class aEleType>
SafePtr<aEleType> MtInQueue::pop() noexcept
{
    static_assert(!std::is_void_v<aEleType>,
        "(MtQ) aEleType can't be void: its lastType is the type before erasure");
    // nothing
    auto&& it = begin_();
    if (it == cache_.end())
        return nullptr;

    // mismatch: leave it
    if (it->lastType() != typeid(aEleType))
        return nullptr;

    // pop
    auto ele = std::move(*it);
    cache_.pop_front();
    return safe_cast<aEleType>(ele);
}

// ***********************************************************************************************
template<class aEleType>
bool MtInQueue::setHdlrOK(EleHdlr aHdlr) noexcept
{
    static_assert(!std::is_void_v<aEleType>,
        "(MtQ) aEleType can't be void: its lastType is the type before erasure");
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
        EMG("(MtQ) except=%s in setHdlrOK", mt_exceptInfo());
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
// 2026-10-14  CSZ       - EMG in rare err
// 2026-11-04  CSZ       - enhance push safety for SafeWeak/weak_ptr
// 2026-11-04  CSZ       - dec mem
// ***********************************************************************************************
