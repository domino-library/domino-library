/**
 * Copyright 2023 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Why/VALUE:
//   . help eg MsgSelf, ThreadBack, MtInQueue to wakeup main thread by mt_pingMainTH()
//     . simpler than para to constructing above class(es)
//   . can base on MT_Notifier
//     . this file is an example, users can define their own MT_PingMainTH.hpp - eg condition_variable
// - MT safe: yes
// - mem safe: yes
// ***********************************************************************************************
#pragma once

#include <thread>

#include "MT_Notifier.hpp"

namespace rlib
{
// ***********************************************************************************************
// * get the process's 1 & ONLY *logical* main thread id (where all logic runs)
//   . unchangable (safe)
//   . NOT necessarily the physical main thread: eg a framework may spawn a thread as "main"
//   . lazy: the 1st caller is fixed as the main (function-local static) & logged once as INF
//   . thread-safe; every later call just returns that same id
// - unify & align all main-thread logic here
// - to check "am I main": mt_getMainTH() == std::this_thread::get_id()
std::thread::id mt_getMainTH() noexcept;

// * check if the current thread is the main thread
//   . no zero-overhead way yet to guard every main-thread entry, so defer until better way
//   . existing call sites stay; on violation: EMG, no throw, never abort
//   . ret: true if in main-thread; false so caller MUST early-return
// - usage: if (!mt_reqMainTH(__func__)) return;
[[nodiscard]] bool mt_reqMainTH(const char* aFn) noexcept;

// ***********************************************************************************************
// - can't use ObjAnywhere that is not MT safe
// - REQ: usr shall not use mt_g_notifMainTH, otherwise impl change may impact his/her code
extern MT_Notifier mt_g_notifMainTH;

// - REQ: can provide diff impl w/o usr code change
inline void mt_pingMainTH()
{
    mt_g_notifMainTH.mt_notify();
}

// - REQ: can provide diff impl w/o usr code change
// - no mt_ since only main thread shall call it
// - REQ: timer guarantee no wait forever
inline void timedwait(const size_t aSec = 0, const size_t aRestNsec = 100'000'000)
{
    mt_g_notifMainTH.timedwait(aSec, aRestNsec);
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2023-10-25  CSZ       1)create
// 2026-10-14  CSZ       - EMG in rare err
// ***********************************************************************************************
