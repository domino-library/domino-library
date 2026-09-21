/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gtest/gtest.h>

#include "UtSoak.hpp"

namespace rlib
{
struct SoakIterListener : testing::EmptyTestEventListener
{
    void OnTestIterationStart(const testing::UnitTest& ut, int it) override
    {
        soakIter_ = it;
        if (it == 0) soakSeed0_ = static_cast<int>(ut.random_seed());
    }
    void OnTestIterationEnd(const testing::UnitTest& ut, int it) override
    {
        std::cerr << "iter=" << it << " seed=" << ut.random_seed()
            << " rss=" << rssBytes() << '\n' << std::flush;
    }
};

// relies on lib_ut being OBJECT lib; STATIC would drop this TU
[[maybe_unused]] static const bool s_soakReg =
    isSoak() && (testing::UnitTest::GetInstance()->listeners().Append(new SoakIterListener), true);

}  // namespace
