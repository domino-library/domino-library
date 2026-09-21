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
#include <memory>

#include "MsgSelf.hpp"
#include "ObjAnywhere.hpp"
#include "ThreadBack.hpp"

namespace rlib
{
// ***********************************************************************************************
// - deferred by MsgSelf: aBackFN runs in main thread via MsgSelf queue, not directly
inline void cb_backFN(TaskBackFN aBackFN, const S_PTR<MsgSelf>& aMsgSelf,
    EMsgPriority aPri, SafePtr<void> aRet) noexcept
{
    if (!aMsgSelf->newMsgOK(
        [aBackFN = std::move(aBackFN),
        ret = std::move(aRet)]() mutable noexcept { aBackFN(std::move(ret)); }, aPri)
    ) {
        ERR("(viaMsgSelf) Failed to newMsgOK for aPri=" << aPri);
    }
}

// wrap TaskBackFN to MsgSelf
[[nodiscard]] inline
TaskBackFN viaMsgSelf(TaskBackFN aBackFN, EMsgPriority aPri = EMsgPri_NORM) noexcept
{
    auto&& msgSelf = MSG_SELF;
    return ! aBackFN || msgSelf.get() == nullptr
        ? TaskBackFN()  // empty fn
        : [aBackFN = std::move(aBackFN), msgSelf, aPri](SafePtr<void> aRet) mutable noexcept
        {
            cb_backFN(std::move(aBackFN), msgSelf, aPri, std::move(aRet));  // not exe here
        };
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2023-09-15  CSZ       1)create
// 2024-10-05  CSZ       - simpler by MSG_SELF instead of para
// ***********************************************************************************************
