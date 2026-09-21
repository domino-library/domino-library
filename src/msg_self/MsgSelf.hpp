/**
 * Copyright 2016 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Issue/why: not callback directly but wait current function return to main() then callback
//   . avoid callback-loop, but exe after all func in-stack returned
//   * avoid logic surprise when immediate callback [MUST-HAVE!]
//   * priority FIFO msg (eg "abort" is higher priority)
//   . can withdraw on-road MsgCB (eg HdlrDomino.rmHdlr())
//
// - how:
//   . newMsgOK(): send msgHdlr into msgQueues_ (all info are in msgHdlr so func<void()> is enough)
//   . handleAllMsg(): call all msgHdlr in msgQueues_, priority then FIFO
//   . single thread (in main thread)
//   * support diff cb mechanism (async, IM, syscom, etc)
//
// - core: msgQueues_[priority][FIFO]
//   . msgQueues_ is a 2D array, 1st dim is priority, 2nd dim is FIFO
//   . perf better than priority_queue that need search & insert for newMsgOK()
//
// - which way?    speed                   UT                           code
//   . async task  may slow if async busy  no but direct-CB instead     simple
//   . IM          may ensure speed        no but direct-CB instead     Im, ActiveBtsL, StatusMo
//   . syscom      may ensure speed        ?                            ?
//   . (conclusion: async for product, direct-CB for UT)
// - why MsgCB instead of WeakMsgCB in msgQueues_?
//   . most users want MsgSelf (instead of themselves) to store cb (naturally; so MsgCB is better)
//   . while only a few want to be able to withdraw cb in msgQueues_ (eg HdlrDomino; so WeakMsgCB is better)
//
// - class safe: yes
//   . not responsible for MsgCB itself's any unsafe behavior
//   . shared_ptr is safe since internal use only (pub-interface is MsgCB&)
//     . principle: safe class can base on unsafe materials
//     . SafePtr is just to test SafeWeak in real world
//   . not support callback after ~MsgSelf() since all msg discarded when ~MsgSelf()
//     . UtInitObjAnywhere gives example how to provide a common callback for main()
//   . not limit queue size as std lib, usr responds for safety
// ***********************************************************************************************
#pragma once

#include <array>
#include <deque>
#include <functional>

#include "MT_PingMainTH.hpp"
#include "UniLog.hpp"
#include "UniPtr.hpp"

#define MSG_SELF (ObjAnywhere::getObj<MsgSelf>())

namespace rlib
{
// ***********************************************************************************************
enum EMsgPriority : unsigned char
{
    EMsgPri_MIN,

    EMsgPri_LOW = EMsgPri_MIN,
    EMsgPri_NORM,
    EMsgPri_HIGH,

    EMsgPri_MAX
};

// !!! MsgCB shall NEVER throw exception!!!
// - MsgCB can try-catch all exception
// - exception is bug to be fixed than pretected
using MsgCB        = std::function<void()>;
using WeakMsgCB    = W_PTR<MsgCB>;
using SharedMsgCB  = S_PTR<MsgCB>;

// ***********************************************************************************************
class MsgSelf : public UniLog
{
public:
    explicit MsgSelf(const LogName& aUniLogName = ULN_DEFAULT) noexcept : UniLog(aUniLogName) {}
    ~MsgSelf() noexcept { if (nMsg_) WRN("discard nMsg=" << nMsg_); }

    MsgSelf(const MsgSelf&)            = delete;
    MsgSelf& operator=(const MsgSelf&) = delete;
    MsgSelf(MsgSelf&&)                 = delete;
    MsgSelf& operator=(MsgSelf&&)      = delete;

    [[nodiscard]] bool   newMsgOK(MsgCB, const EMsgPriority = EMsgPri_NORM) noexcept;
    [[nodiscard]] size_t nMsg() const noexcept { return nMsg_; }
    [[nodiscard]] size_t nMsg(const EMsgPriority aPri) const noexcept { return isValidPri(aPri) ? msgQueues_[aPri].size() : 0; }
    void handleAllMsg() noexcept;

    [[nodiscard]] static constexpr bool isLowPri(const EMsgPriority aPri) noexcept { return aPri < EMsgPri_NORM; }
    [[nodiscard]] static constexpr bool isValidPri(const EMsgPriority aPri) noexcept { return aPri < EMsgPri_MAX; }

private:
    bool handleOneMsg_() noexcept;

    // -------------------------------------------------------------------------------------------
    std::array<std::deque<MsgCB>, EMsgPri_MAX> msgQueues_;
    size_t nMsg_ = 0;
};
}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2016-12-02  CSZ       1)create & support priority
// 2019-11-06  CSZ       - lower priority by 1msg/handleAllMsg
// 2020-04-21  CSZ       - no crash to callback handleAllMsg() after MsgSelf is deleted
// 2020-08-10  CSZ       2)refactory to MsgSelf + AsyncLooper/ShortLooper/EmptyLooper/etc
// 2020-12-28  CSZ       3)fix invalid cb by weak_ptr<cb>
// 2021-04-05  CSZ       - coding req
// 2021-11-29  CSZ       - lambda instead of bind; fix isValid ut
// 2022-01-01  PJ & CSZ  - formal log & naming
// 2022-03-10  CSZ       - inc code coverage rate
// 2022-08-18  CSZ       - replace CppLog by UniLog
// 2022-11-04  CSZ       4)simplify newMsgOK(WeakMsgCB -> MsgCB)
//                         . easier to user
//                         . support both MsgCB & WeakMsgCB(by lambda)
// 2022-12-02  CSZ       - simple & natural
// 2022-12-31  CSZ       - not support MsgCB=nullptr
// 2023-07-13  CSZ       - copilot compare
// 2023-10-27  CSZ       - replace pingMainFN_() by mt_pingMainTH()
// 2025-02-13  CSZ       - support both SafePtr & shared_ptr
// 2025-03-25  CSZ       5)enable exception: tolerate is safer; can't recover except->terminate
// ***********************************************************************************************
