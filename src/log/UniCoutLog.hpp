/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - ISSUE/why:
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

#include "UniBaseLog.hpp"

namespace rlib
{
// ***********************************************************************************************
class UniCoutLog
{
public:
    explicit UniCoutLog(const LogName&) noexcept {}  // compatible UniSmartLog
    UniCoutLog() = default;

    static std::ostream& oneLog() noexcept;
    std::ostream& operator()() const noexcept { return oneLog(); }
    static void trcPrintf(const char* fmt, ...) noexcept __attribute__((format(printf, 1, 2)));

    static void needLog() noexcept {}
    static LogName uniLogName() noexcept { return ULN_DEFAULT; }
    static size_t nLog() noexcept { return 1; }

    [[nodiscard]] static bool setLogFileOK(const std::string& aFileName) noexcept;

private:
    static void resetTrcFp_(std::FILE* aNewFp = stdout) noexcept {
        auto* old = trcFp_.exchange(aNewFp, std::memory_order_acq_rel);
        if (old != stdout && old != nullptr) std::fclose(old);
    }

    // -------------------------------------------------------------------------------------------
public:
    static UniCoutLog              defaultUniLog_;
    static std::atomic<size_t>     nLogLine_;  // ut only, simpler here
    static std::ostream*           out_;
    static std::ofstream           file_;
    static std::atomic<std::FILE*> trcFp_;  // TRC()

#ifdef IN_ALL_UT
    // -------------------------------------------------------------------------------------------
    // MT safe : no (since nLogLine_ is not atomic & no worth for ut only)
    // mem safe: yes
public:
    static void dropAllBuf_forUt() noexcept {}    // SmartLog dual; cout has no buf
    static void forceSaveAll_forUt() noexcept {}  // already on cout

    static void dumpAll_forUt() {  // for ut case clean at the end
        nLogLine_ = 0;
        out_ = &std::cout;
        file_.close();
        resetTrcFp_();
    }
    static size_t logLen(const LogName& = ULN_DEFAULT) { return nLogLine_; }
#endif
};

// ***********************************************************************************************
// static than inline, avoid ut conflict when coexist both UniLog
[[maybe_unused]] static std::ostream& oneLog() { return UniCoutLog::oneLog(); }

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
// ***********************************************************************************************
