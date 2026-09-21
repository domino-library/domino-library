/**
 * Copyright 2023 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <algorithm>
#include <cerrno>
#include <thread>
#include <time.h>

#include "MT_Notifier.hpp"
#include "MT_PingMainTH.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
void MT_Notifier::mt_notify() noexcept
{
    // - safest to sem_post() directly, avoid complex sync scenario
    // - EOVERFLOW harmless: sem stays MAX, wait still works
    sem_post(&mt_sem_);
}

// ***********************************************************************************************
void MT_Notifier::timedwait(const size_t aSec, const size_t aRestNsec) noexcept
{
    if (! mt_reqMainTH(__func__))
        return;

    timespec ts{0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ts);  // clock-immune

    const auto ns = ts.tv_nsec + min(aRestNsec, size_t(999'999'999));  // clamp: aRestNsec must < 1s; overflow-safe
    ts.tv_sec += (aSec + ns / 1'000'000'000);
    ts.tv_nsec = ns % 1'000'000'000;

    for (;;)
    {
        const auto ret = sem_clockwait(&mt_sem_, CLOCK_MONOTONIC, &ts);  // clock-immune
        // - notified(include EOVERFLOW on sem_post side) or timeout
        // - safe: must check ret firstly; safer to check ret=-1
        if (ret == 0 || (ret == -1 && errno == ETIMEDOUT))
        {
            // - sem_trywait(): want to reduce counter to 0 so no immediate next wakeup
            // - limit=100: cost little time (but counter may not 0 rarely)
            for (int i = 0; i < 100 && sem_trywait(&mt_sem_) == 0; ++i);
            return;
        }
        if (ret == -1 && errno != EINTR)
            return;  // not timeout/notify/EINTR (eg EINVAL): stop, don't loop
        // EINTR: retry until timeout or notify
    }
}

}  // namespace
