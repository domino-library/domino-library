/**
 * Copyright 2023 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - VALUE/why:
//   . align with MsgSelf since all "msg" shall queue in MsgSelf
//   . viaMsgSelf() out of ThreadBack so ThreadBack can standalone without MsgSelf
//   . same for MsgSelf
//   . avoid block main thread
//
// - mem safe: yes
// ***********************************************************************************************
#pragma once

#include "MsgSelf.hpp"
#include "ThreadBack.hpp"

namespace rlib
{
// ***********************************************************************************************
// - deferred by MsgSelf: aBackFN runs in main thread via MsgSelf queue, not directly
inline void cb_backFN(TaskBackFN aBackFN, const SPtr<MsgSelf>& aMsgSelf,
    EMsgPriority aPri, SafePtr<void> aRet) noexcept
{
    if (!aMsgSelf->newMsgOK(
        [aBackFN = std::move(aBackFN),
        ret = std::move(aRet)]() mutable noexcept
        {
            try { aBackFN(std::move(ret)); }
            catch (...) { ERR("(viaMsgSelf) backFN() except=" << mt_exceptInfo()); }
        }, aPri)
    ) {
        ERR("(viaMsgSelf) Failed to newMsgOK for aPri=" << aPri);
    }
}

// wrap TaskBackFN to MsgSelf
[[nodiscard]] inline
TaskBackFN viaMsgSelf(TaskBackFN aBackFN, EMsgPriority aPri = EMsgPri_NORM) noexcept
{
    auto ms = getMsgSelf();
    return ! aBackFN || ! ms
        ? TaskBackFN()  // empty fn
        : [aBackFN = std::move(aBackFN), ms, aPri](SafePtr<void> aRet) mutable noexcept
        {
            cb_backFN(std::move(aBackFN), ms, aPri, std::move(aRet));  // not exe here
        };
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2023-09-15  CSZ       1)create
// 2024-10-05  CSZ       - simpler by MSG_SELF instead of para
// 2026-10-19  CSZ       - catch backFN() here; MsgCB itself does not throw
// ***********************************************************************************************
