/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <utility>
#include "ThPoolBack.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
ThPoolBack::ThPoolBack(size_t aMaxThread, size_t aMaxTaskQ)
{
    try {
        // validate
        if (aMaxThread == 0)
        {
            WRN("!!! aMaxThread=0 is meaningless, force to " << MAX_THREAD);
            aMaxThread = MAX_THREAD;
        }
        if (aMaxTaskQ == 0)
        {
            WRN("!!! aMaxTaskQ=0 is meaningless, force to " << MAX_TASKQ);
            aMaxTaskQ = MAX_TASKQ;
        }

        // create threads
        thPool_.reserve(aMaxThread);  // not construct any thread
        maxInFlight_ = aMaxTaskQ;  // construct-time limit for limitNewTaskOK
        for (size_t i = 0; i < aMaxThread; ++i)
            thPool_.emplace_back(&ThPoolBack::mt_threadMain_, this);
    } catch(...) {  // ut can't cover this branch; rare but safer
        clean_();
        EMG("(ThPoolBack) Except=%s when creating nThread=%zu", mt_exceptInfo(), aMaxThread);
        throw;  // break constructor
    }
}

// ***********************************************************************************************
void ThPoolBack::mt_threadMain_() noexcept
{
    for (;;)
    {
        list<MT_ThTaskInfo_> mine;
        {
            // - lock to prevent new task/notif until my mt_qCv_ sleep/wait-notif (ensure not loss notif)
            // - lock to prevent other thread steal task
            unique_lock lock(mt_mutex_);
            mt_qCv_.wait(lock,
                [this]() noexcept { return mt_stopAllTH_ || !mt_taskQ_.empty(); });

            if (mt_stopAllTH_)
                return;
            // mt_qCv_.wait(): lock then check predicate, so no need check mt_taskQ_.empty() here

            // splice(): no alloc & except - safer & simpler
            mine.splice(mine.end(), mt_taskQ_, mt_taskQ_.begin());  // mv 1st task to mine
        }

        // exception stays on this thread (same as AsyncBack). backFN sees a null ret.
        // ~backFN runs later, when the main thread drops the node.
        auto& task = mine.front();
        try { task.mt_ret_ = task.mt_entryFN_(); }
        catch (...) {
            EMG("(ThPoolBack) entryFN() except=%s", mt_exceptInfo());
        }
        task.mt_entryFN_ = nullptr;  // drop captures before main can run backFN_

        {
            lock_guard lock(mt_mutex_);
            mt_doneQ_.splice(mt_doneQ_.end(), mine);  // no alloc & except - safer & simpler
        }
        mt_pingMainTH();  // always ping, or may wait long under low load
    }
}

// ***********************************************************************************************
ThPoolBack::~ThPoolBack() noexcept
{
    clean_();
}

// ***********************************************************************************************
void ThPoolBack::clean_() noexcept
{
    {
        // must hold mutex: otherwise worker may check stop=false, miss notify, then wait forever
        lock_guard lock(mt_mutex_);
        mt_stopAllTH_.store(true, std::memory_order_release);
    }
    mt_qCv_.notify_all();

    for (auto&& th : thPool_)
        if (th.joinable())
            th.join();  // safer: avoid terminate when destruct th
    // Joined: each worker has already spliced its node into mt_doneQ_. Do not run backFN (it may newTaskOK()).
    if (nInFlight_ != 0)
        WRN("(ThPoolBack) discard nFut=" << nInFlight_);
    mt_taskQ_.clear();
    mt_doneQ_.clear();
    nInFlight_ = 0;
}

// ***********************************************************************************************
size_t ThPoolBack::hdlDoneFut(UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return 0;

    // Snapshot: a backFN that submits more work must not keep this call draining forever.
    // list's default ctor does not allocate, and swap does not throw.
    list<MT_ThTaskInfo_> done;
    {
        lock_guard lock(mt_mutex_);
        done.swap(mt_doneQ_);
    }

    size_t nHandled = 0;
    while (! done.empty())
    {
        auto& task = done.front();
        --nInFlight_;  // before backFN, so limitNewTaskOK sees the freed slot
        try { task.backFN_(move(task.mt_ret_)); }
        catch (...) { ERR("(ThPoolBack) backFN() except=" << mt_exceptInfo()); }
        done.pop_front();  // ~backFN on this (main) thread
        ++nHandled;
    }
    return nHandled;
}

// ***********************************************************************************************
bool ThPoolBack::newTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    try {
        if (! newTaskArgOK_(mt_aEntryFN, aBackFN, oneLog))
            return false;

        // Only allocation for this task. If it throws, ~MT_ThTaskInfo_ runs on this thread.
        // splice into the shared list does not allocate.
        list<MT_ThTaskInfo_> one;
        one.push_back(MT_ThTaskInfo_{std::move(mt_aEntryFN), std::move(aBackFN), {}});  // GCOVR_EXCL_BR_LINE bad_alloc only
        {
            lock_guard lock(mt_mutex_);
            mt_taskQ_.splice(mt_taskQ_.end(), one);
            ++nInFlight_;
        }
        mt_qCv_.notify_one();  // notify thread pool to run a new task

        return true;
    } catch(...) {  // ut can't cover: push_back throws only on bad_alloc
        // node was not queued; ~backFN already ran on this thread
        EMG("(ThPoolBack) except=%s in newTaskOK", mt_exceptInfo());
        return false;  // GCOVR_EXCL_LINE
    }
}

}  // namespace
