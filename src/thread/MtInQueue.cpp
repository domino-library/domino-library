/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <algorithm>

#include "MtInQueue.hpp"
#include "MT_PingMainTH.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
MtInQueue::~MtInQueue() noexcept
{
    if (const auto nEle = size(true))
        WRN("(MtQ) discard nEle=" << nEle);  // main thread can WRN()
}

// ***********************************************************************************************
deque<SafePtrOver<void>>::iterator MtInQueue::begin_() noexcept
{
    if (cache_.empty())
    {
        unique_lock guard(mt_mutex_, try_to_lock);  // avoid block main thread
        if (! guard.owns_lock())
        {
            mt_pingMainTH();  // since waste this wakeup as not own the lock
            this_thread::yield();  // avoid main thread keep checking
            return cache_.end();
        }
        if (mt_queue_.empty())
            return cache_.end();
        cache_.swap(mt_queue_);  // fast & for at most ele
    }
    // unlocked

    // HID("(MtQ) ptr=" << cache_.begin()->get() << ", nRef=" << cache_.begin()->use_count());
    return cache_.begin();
}

// ***********************************************************************************************
void MtInQueue::clearAll() noexcept
{
    if (! mt_reqMainTH(__func__))
        return;

    // Don't destruct user objects while holding mt_mutex_: their destructor may re-enter mt_pushOK().
    deque<SafePtrOver<void>> discarded;
    {
        lock_guard guard(mt_mutex_);
        discarded.swap(mt_queue_);
    }
    cache_.clear();  // can't access outside main thread!!!
    decltype(tid_hdlr_S_)().swap(tid_hdlr_S_);
}

// ***********************************************************************************************
void MtInQueue::clearHdlrPool() noexcept
{
    decltype(tid_hdlr_S_)().swap(tid_hdlr_S_);
}

// ***********************************************************************************************
size_t MtInQueue::handleAllEle() noexcept
{
    if (! mt_reqMainTH(__func__))
        return 0;

    const auto nEle = handleCacheEle_();

    {
        unique_lock guard(mt_mutex_, try_to_lock);  // avoid block main thread
        if (! guard.owns_lock())
        {
            mt_pingMainTH();  // for possible ele in mt_queue_
            this_thread::yield();  // avoid main thread keep checking; no ut for optimization
            return nEle;
        }
        cache_.swap(mt_queue_);
    }
    return nEle + handleCacheEle_();
}

// ***********************************************************************************************
size_t MtInQueue::handleCacheEle_() noexcept
{
    const auto nEle = cache_.size();
    while (! cache_.empty())
    {
        auto ele = move(cache_.front());  // avoid cp; lastType_ is the type key
        cache_.pop_front();
        const auto tid = ele.lastType();

        auto&& id_hdlr = find_if(
            tid_hdlr_S_.begin(), tid_hdlr_S_.end(),
            [&](auto& tid_hdlr) { return tid_hdlr.first == tid; }
        );
        if (id_hdlr == tid_hdlr_S_.end())
        {
            WRN("(MtQ) discard 1 ele(=" << tid.name() << ") since no handler.");
            continue;
        }

        // Copy first. setHdlrOK()/clearHdlrPool() may reallocate or drop tid_hdlr_S_
        // while this call is still on the stack.
        try {
            EleHdlr hdlr = id_hdlr->second;
            hdlr(SafePtr<void>(std::move(ele)));  // crossed
        }
        catch(...) {
            ERR("(MtQ) hdlr() except=" << mt_exceptInfo() << ", tid=" << tid.name());
        }  // continue next ele
    }  // while
    return nEle;
}

// ***********************************************************************************************
SafePtrOver<void> MtInQueue::pop() noexcept
{
    // nothing
    auto&& it = begin_();
    if (it == cache_.end())
        return nullptr;

    // pop
    auto ele = move(*it);  // take it out before pop; a ref into cache_ dangles after pop_front
    cache_.pop_front();
    return ele;
}

// ***********************************************************************************************
size_t MtInQueue::size(bool canBlock) const noexcept
{
    if (! mt_reqMainTH(__func__))
        return 0;

    if (canBlock)
    {
        lock_guard guard(mt_mutex_);
        return mt_queue_.size() + cache_.size();  // can't access outside main thread!!!
    }

    // non block
    unique_lock tryGuard(mt_mutex_, try_to_lock);
    //HID(__LINE__ << " owns=" << tryGuard.owns_lock());
    return tryGuard.owns_lock()
        ? mt_queue_.size() + cache_.size()
        : cache_.size();  // can't access outside main thread!!!
}

// ***********************************************************************************************
MtInQueue& mt_getQ() noexcept
{
    // - not exist if nobody call this func
    // - c++11: function-local static init is MT safe
    // - can't via ObjAnywhere that's not MT safe
    static MtInQueue s_mtQ;

    return s_mtQ;
}

}  // namespace
