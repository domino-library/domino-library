/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#define IN_GTEST
#include "UniSmartLog.hpp"
#undef IN_GTEST

#define UNI_LOG_TEST UniSmartLogTest
#define UNI_LOG      UniSmartLog
#include "UniLogTest.hpp"

// ***********************************************************************************************
namespace rlib
{
// - end-to-end: TRC writes to DEFAULT SmartLog, TRC-off produces nothing
TEST_F(UniSmartLogTest, TRC_endToEnd_smartLogOutput_and_traceOff)
{
    // TRC-on: trace appears in defaultUniLog_'s SmartLog buffer
    const auto lenBefore = UniSmartLog::defaultUniLog_.trcOut().size();
    TRC("event %s id=%d", "setPrev", 42);
    const auto lenAfterOn = UniSmartLog::defaultUniLog_.trcOut().size();
    EXPECT_GT(lenAfterOn, lenBefore) << "REQ: TRC-on writes to DEFAULT SmartLog";

    // TRC-off: no additional output (TRC macro checks traceOn_)
    traceOn_ = false;
    TRC("should not appear %d", 99);
    const auto lenAfterOff = UniSmartLog::defaultUniLog_.trcOut().size();
    traceOn_ = true;
    EXPECT_EQ(lenAfterOn, lenAfterOff) << "REQ: TRC-off produces no output";
}

// - end-to-end: TRC safely truncates long messages (buf[256]) into SmartLog
TEST_F(UniSmartLogTest, TRC_longMessage_truncatedInSmartLog)
{
    const auto lenBefore = UniSmartLog::defaultUniLog_.trcOut().size();
    const std::string longMsg(300, 'Z');
    TRC("%s", longMsg.c_str());
    const auto lenAfter = UniSmartLog::defaultUniLog_.trcOut().size();
    const auto written = lenAfter - lenBefore;
    EXPECT_GT(written, 0u) << "REQ: truncated TRC still produces output";
    EXPECT_LE(written, 256u) << "REQ: output truncated to buf size";
}

// - BaseSL: SmartLog lifecycle — canDelLog reflects needLog state
TEST_F(UniSmartLogTest, canDelLog_reflectsNeedLog)
{
    StrCoutFSL log;
    EXPECT_TRUE(log.canDelLog()) << "REQ: new log can be deleted (no needLog)";
    log.needLog();
    EXPECT_FALSE(log.canDelLog()) << "REQ: after needLog(), canDelLog() returns false";
}

// - soak: dump all live SmartLogs without name_log_S_.clear() (dropAllBuf is on every OA TearDown)
TEST_F(UniSmartLogTest, forceSaveAll_dumpsThenClears)
{
    ClassUsr usr(logName_);
    ASSERT_GT(UNI_LOG::logLen(logName_), 0u);
    UNI_LOG::forceSaveAll_forUt();
    EXPECT_EQ(0u, UNI_LOG::logLen(logName_)) << "REQ: dump then clear, log object kept";
}

}  // namespace rlib
