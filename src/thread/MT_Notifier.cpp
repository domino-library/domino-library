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
        if (ret == -1 && errno == EINTR)
            continue;  // retry until timeout or notify
        // notified (incl EOVERFLOW on post), timeout, or unrecoverable (eg EINVAL): stop
        for (int i = 0; i < 100 && sem_trywait(&mt_sem_) == 0; ++i);  // drain: no immediate next wakeup
        return;
    }
}

}  // namespace
