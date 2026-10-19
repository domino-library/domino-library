/**
 * Copyright 2024 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <utility>

#include "MT_PingMainTH.hpp"
#include "ThreadBack.hpp"

using namespace std;

namespace rlib
{
// ***********************************************************************************************
bool ThreadBack::limitNewTaskOK(MT_TaskEntryFN mt_aEntryFN, TaskBackFN aBackFN, UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return false;

    if (nFut() >= maxInFlight_)
    {
        WRN("(ThreadBack) max reached (max=" << maxInFlight_ << "), reject new task");
        return false;
    }
    return newTaskOK(std::move(mt_aEntryFN), std::move(aBackFN), oneLog);
}

// ***********************************************************************************************
bool ThreadBack::newTaskArgOK_(const MT_TaskEntryFN& mt_aEntryFN, const TaskBackFN& aBackFN, UniLog& oneLog) noexcept
{
    if (! mt_reqMainTH(__func__))
        return false;

    if (! aBackFN)
    {
        ERR("(ThreadBack) aBackFN=null doesn't make sense!!! Why not async() directly?");
        return false;
    }
    if (! mt_aEntryFN)
    {
        ERR("(ThreadBack) NOT support mt_aEntryFN=null!!! Necessary?");
        return false;
    }
    return true;
}

}  // namespace
