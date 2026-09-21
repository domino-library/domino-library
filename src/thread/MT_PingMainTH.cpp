/**
 * Copyright 2023 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <atomic>

#include "MT_PingMainTH.hpp"
#include "UniLog.hpp"

namespace rlib
{
alignas(64) MT_Notifier g_notifMainTH;

// ***********************************************************************************************
// - better than in hpp: avoid multi-copy in diff cpp/lib
// - c++ not support common way to get physical main thread id
//   * but logical main thread is better than physical, eg in some framework that spawn a thread as "main"
std::thread::id mt_getMainTH() noexcept
{
    static const auto s_mainTH = []
    {
        const auto id = std::this_thread::get_id();  // 1st caller is fixed as the logical main
        INF("(MainTH) 1st call, set current thread as the 1 & ONLY logical-main-thread=" << id);
        return id;
    }();
    return s_mainTH;
}

// ***********************************************************************************************
bool mt_reqMainTH(const char* aFn) noexcept
{
    if (mt_getMainTH() == std::this_thread::get_id())  // fast path: normal case
        return true;

    // repeat alarm
    ERR("(MainTH) !!! " << aFn << "() must in main-thread=" << mt_getMainTH()
        << ", not=" << std::this_thread::get_id());
    return false;
}
}  // namespace