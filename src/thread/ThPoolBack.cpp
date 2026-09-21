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
        fut_backFN_S_.reserve(aMaxTaskQ);  // safe for limitNewTaskOK; better perf for most cases
        maxParallel_ = aMaxTaskQ;  // construct-time limit for limitNewTaskOK
        for (size_t i = 0; i < aMaxThread; ++i)
            thPool_.emplace_back(&ThPoolBack::mt_threadMain_, this);
    } catch(...) {  // ut can't cover this branch; rare but safer
        clean_();
        ERR("(ThPoolBack) Except=" << mt_exceptInfo() << " when creating nThread=" << aMaxThread);
        throw;  // break constructor
    }
}

// ***********************************************************************************************
void ThPoolBack::mt_threadMain_() noexcept
{
    for (;;)
    {
        packaged_task<SafePtr<void>()> task;
        {
            // - lock to prevent new task/notif until my mt_qCv_ sleep/wait-notif (ensure not loss notif)
            // - lock to prevent other thread steal task
            unique_lock lock(mt_mutex_);
            mt_qCv_.wait(lock,
                [this]() noexcept { return mt_stopAllTH_ || !mt_taskQ_.empty(); });

            if (mt_stopAllTH_)
                return;
            // mt_qCv_.wait(): lock then check predicate, so no need check mt_taskQ_.empty() here

            task = move(mt_taskQ_.front());
            mt_taskQ_.pop_front();
        }

        // - thread can continue when task() throw
        // - other excepts (eg bad_alloc) are rare & hard-recover
        task();  // packaged_task saves exception in its future

        // no lock so can only use MT_safe part in "this"
        mt_nDoneFut_.fetch_add(1, std::memory_order_release);
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

    // handle completed-but-unprocessed tasks before destruction
    (void)hdlDoneFut();
    if (!fut_backFN_S_.empty())
        HID("(ThPoolBack) discarding " << fut_backFN_S_.size() << " pending tasks (backFN not called)");
}

// ***********************************************************************************************
bool ThPoolBack::newTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    try {
        if (! ThreadBack::newTaskOK(mt_aEntryFN, aBackFN, oneLog))
            return false;

        packaged_task<SafePtr<void>()> task(std::move(mt_aEntryFN));  // packaged_task can get_future()="task result"
        fut_backFN_S_.push_back(Fut_BackFN{task.get_future(), std::move(aBackFN)});  // save future & backFN
        {
            lock_guard lock(mt_mutex_);
            mt_taskQ_.emplace_back(move(task));
        }
        mt_qCv_.notify_one();  // notify thread pool to run a new task

        return true;
    } catch(...) {  // ut can't cover this branch; rare but safer
        ERR("(ThPoolBack) except=" << mt_exceptInfo() << " in newTaskOK");
        return false;
    }
}

}  // namespace
