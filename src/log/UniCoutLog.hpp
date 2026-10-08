/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - ISSUE/why:
//   . MT safe log:
//     . UniSmartLog is NOT; UniCoutLog is YES & simplest
//     . INF/WRN/ERR/HID: MT safe
//     . TRC: highest perf; MT safe
//   . encapsulate cout/file for eg:
//     . UT
//     . simplest log for debug
//     . gtest case destructor can't catch EXPECT_CALL() failure, so SmartLog doesn't work
//     . other unknown issue(s) that SmartLog can't fix
//
// - CORE:
//   . out_
//
// - MT safe : yes
// - class safe: yes
// ***********************************************************************************************
#pragma once

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>

#include "UniBaseLog.hpp"

namespace rlib
{
// ***********************************************************************************************
class UniCoutLog
{
public:
    explicit UniCoutLog(const LogName&) noexcept {}  // compatible UniSmartLog
    UniCoutLog() = default;

    // holds coutMutex_() until the temporary dies, so `oneLog() << a << b` is one critical section
    class Line
    {
    public:
        Line() : lock_(coutMutex_()), os_(out_) {}  // lock_ is declared first, so out_ is read under the lock

        template<class T>
        Line& operator<<(T&& aVal) noexcept
        {
            try { *os_ << static_cast<T&&>(aVal); } catch (...) {}
            return *this;
        }
        Line& operator<<(std::ostream& (*aManip)(std::ostream&)) noexcept  // endl/flush are templates
        {
            try { *os_ << aManip; } catch (...) {}
            return *this;
        }

    private:
        std::unique_lock<std::recursive_mutex> lock_;
        std::ostream* os_;
    };

    static Line oneLog() noexcept;
    Line operator()() const noexcept { return oneLog(); }
    static void trcPrintf(const char* fmt, ...) noexcept __attribute__((format(printf, 1, 2)));

    static void needLog() noexcept {}
    static LogName uniLogName() noexcept { return ULN_DEFAULT; }
    static size_t nLog() noexcept { return 1; }

    [[nodiscard]] static bool setLogFileOK(const std::string& aFileName) noexcept;

private:
    // nullptr = stdout. dup2 under flockfile; the FILE* stays put so fwrite cannot use-after-free
    static bool redirectTrcOK_(const char* aPath) noexcept;
    static void flushTrc_() noexcept;

    // -------------------------------------------------------------------------------------------
public:
    static UniCoutLog              defaultUniLog_;
    static std::atomic<size_t>     nLogLine_;  // ut only, simpler here
    static std::ostream*           out_;
    static std::ofstream           file_;

#ifdef IN_ALL_UT
    // -------------------------------------------------------------------------------------------
    // MT safe : yes (coutMutex_())
    // mem safe: yes
public:
    static void dropAllBuf_forUt() noexcept {}  // SmartLog dual; cout has no buf
    // abort() keeps cout's C++ buf and, since glibc 2.27, the stdio FILE* buf
    static void forceSaveAll_forUt() noexcept
    {
        std::lock_guard<std::recursive_mutex> guard(coutMutex_());  // workers may still be logging
        out_->flush();
        flushTrc_();
    }
    static void rawDumpAll_forUt() noexcept {}  // cout's buf needs the lock; Die() must not take it

    static void dumpAll_forUt() {  // for ut case clean at the end
        std::lock_guard<std::recursive_mutex> guard(coutMutex_());
        nLogLine_ = 0;
        out_ = &std::cout;
        file_.close();
        redirectTrcOK_(nullptr);
    }
    static size_t logLen(const LogName& = ULN_DEFAULT) { return nLogLine_; }
#endif
};

// ***********************************************************************************************
// static than inline, avoid ut conflict when coexist both UniLog
[[maybe_unused]] static UniCoutLog::Line oneLog() { return UniCoutLog::oneLog(); }

using UniLog = UniCoutLog;

}  // namespace

// ***********************************************************************************************
// - override TRC fallback
// - branch-predict: traceOn_ rarely flips -> well-predicted, ~0 overhead when enabled
#undef TRC
#define TRC(...) do { if (rlib::traceOn_) rlib::UniCoutLog::trcPrintf(__VA_ARGS__); } while(0)

// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2022-08-26  CSZ       1)create
// 2022-12-02  CSZ       - simple & natural
// 2024-02-21  CSZ       2)mem-safe
// 2025-04-07  CSZ       3)tolerate exception
// 2026-03-13  CSZ       4)log to file than cout
// 2026-09-20  CSZ       - dropAllBuf_forUt / forceSaveAll_forUt stubs (SmartLog dual)
// 2026-10-01  CSZ       5)MT safe UniCoutLog
// 2026-10-02  CSZ       - enhance log when soak failed
// 2026-10-08  CSZ       - TRC: MT safe
// ***********************************************************************************************
