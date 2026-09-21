/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - soak helpers: rss, fail dump/stop; per-iteration line lives in UtSoak.cpp
// ***********************************************************************************************
#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "UniLog.hpp"

namespace rlib
{
inline bool isSoak() noexcept { return std::getenv("SOAK") != nullptr; }

inline int soakSeed0_ = 0;
inline int soakIter_ = 0;

inline size_t rssBytes()
{
    size_t rss = 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (f) {
        unsigned long dummy, pages;
        if (fscanf(f, "%lu %lu", &dummy, &pages) == 2)
        {
            const long page = sysconf(_SC_PAGESIZE);
            rss = pages * static_cast<size_t>(page > 0 ? page : 4096);
        }
        fclose(f);
    }
    return rss;
}

inline void soakReplayAndAbort()
{
    UniLog::forceSaveAll_forUt();
    std::cerr << "replay: SEED=" << soakSeed0_ << " ut/soak.sh --gtest_repeat="
        << (soakIter_ + 1) << " rss=" << rssBytes() << '\n' << std::flush;
    std::abort();
}

// after fixture dtor (gmock Times already checked): dump OA logs on fail; always drop buf
inline void finishUniLogAfterCase(bool failed)
{
    if (failed)
    {
        if (isSoak()) soakReplayAndAbort();
        UniLog::forceSaveAll_forUt();
    }
    UniLog::dropAllBuf_forUt();
}

}  // namespace
