/**
 * Copyright 2016 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include "MsgSelf.hpp"

namespace rlib
{
// ***********************************************************************************************
void MsgSelf::handleAllMsg() noexcept
{
    while (handleOneMsg_());  // handleOneMsg_() may create new high priority msg(s)
}

// ***********************************************************************************************
bool MsgSelf::handleOneMsg_() noexcept
{
    for (int msgPri = EMsgPri_MAX - 1; msgPri >= EMsgPri_MIN; --msgPri)
    {
        auto&& oneQueue = msgQueues_[msgPri];
        if (oneQueue.empty())
            continue;

        auto msg = std::move(oneQueue.front());
        oneQueue.pop_front();  // except can't recover->terminate
        --nMsg_;

        try { msg(); } // run 1st MsgCB; newMsgOK() prevent nullptr into msgQueues_
        catch(...) { ERR("(MsgSelf) msg() except=" << mt_exceptInfo()); }

        if (not nMsg())
            return false;    // no more to continue
        if (isLowPri(EMsgPriority(msgPri)))
        {
            mt_pingMainTH();
            return false;    // not continue for low priority until next ping-pong
        }
        return true;
    }
    return false;
}

// ***********************************************************************************************
bool MsgSelf::newMsgOK(MsgCB aMsgCB, const EMsgPriority aMsgPri) noexcept
{
    // validate
    if (! aMsgCB)
    {
        WRN("(MsgSelf) failed!!! aMsgCB=nullptr doesn't make sense.");
        return false;
    }
    if (!isValidPri(aMsgPri))
    {
        WRN("(MsgSelf) failed!!! outbound aMsgPri=" << aMsgPri);
        return false;
    }

    // store
    try {
        msgQueues_[aMsgPri].emplace_back(std::move(aMsgCB));
    } catch(...) {  // ut can't cover this branch; rare but safer
        ERR("(MsgSelf) except=" << mt_exceptInfo() << " in newMsgOK");
        return false;
    }
    ++nMsg_;
    HID("(MsgSelf) nMsg=" << nMsg_);

    // ping main thread
    mt_pingMainTH();
    return true;
}

}  // namespace
