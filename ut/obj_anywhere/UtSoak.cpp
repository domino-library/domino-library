/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gtest/gtest.h>

#include "UtInitObjAnywhere.hpp"
#include "UtSoak.hpp"

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) \
    || __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#include <sanitizer/common_interface_defs.h>
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
// no compile flag for this; default continues. covers make run, soak.sh, ./ut_exe
extern "C" const char* __tsan_default_options() { return "halt_on_error=1"; }
#endif

namespace rlib
{
struct SoakIterListener : testing::EmptyTestEventListener
{
    void OnTestProgramEnd(const testing::UnitTest&) override
    {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) \
    || __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
        __sanitizer_set_death_callback(nullptr);  // LSan Die() is at exit, after statics may be gone
#endif
    }
    void OnTestIterationStart(const testing::UnitTest& ut, int it) override
    {
        soakIter_ = it;
        if (it == 0) soakSeed0_ = static_cast<int>(ut.random_seed());
    }
    void OnTestIterationEnd(const testing::UnitTest& ut, int it) override
    {
        auto nEv = [](auto p) { return p ? p->evNames().size() : 0u; };
        std::cerr << "iter=" << it << " seed=" << ut.random_seed()
            << " rss=" << rssBytes()
            << " nEv=" << nEv(ObjAnywhere::getObj<MinRmEvDom>().get())
            << ',' << nEv(ObjAnywhere::getObj<MaxNofreeDom>().get())
            << ',' << nEv(ObjAnywhere::getObj<MaxDom>().get())
            << '\n' << std::flush;
    }
};

// relies on lib_ut being OBJECT lib; STATIC would drop this TU
[[maybe_unused]] static const bool s_soakReg =
    isSoak() && (testing::UnitTest::GetInstance()->listeners().Append(new SoakIterListener), true);

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) \
    || __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
// Die() skips gtest and may still hold TSan's lock. no cout, no mutex, no malloc.
// UniCoutLog's rawDump is empty: cout's buf needs the lock, so an INF tail can be lost.
// WRN/ERR already flushed. OnTestProgramEnd clears this; LSan Die() is at exit.
[[maybe_unused]] static const bool s_sanDieReg =
    isSoak() && (__sanitizer_set_death_callback([] {
        soakReplayLine();
        UniLog::rawDumpAll_forUt();
    }), true);
#endif

}  // namespace
