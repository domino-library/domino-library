/**
 * Copyright 2024 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "MT_PingMainTH.hpp"
#include "ThreadBack.hpp"

namespace rlib
{
// ***********************************************************************************************
bool ThreadBack::newTaskArgOK_(const MT_SafeThEntryFN& mt_aEntryFN, const UsrBackFN& aBackFN, UniLog& oneLog) noexcept
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
