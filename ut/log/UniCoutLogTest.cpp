/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#define IN_GTEST
#include "UniCoutLog.hpp"
#undef IN_GTEST

#include "StrCoutFSL.hpp"

#include <sstream>

#define UNI_LOG_TEST UniCoutLogTest
#define UNI_LOG      UniCoutLog
#include "UniLogTest.hpp"

// ***********************************************************************************************
namespace rlib
{
TEST_F(UniCoutLogTest, setFileOK_writeAndRestore)
{
    const std::string fname = "ut_log_file_test.log";
    std::remove(fname.c_str());

    // switch to file
    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));
    INF("hello file");
    ASSERT_GT(UniCoutLog::logLen(), 0u);

    // verify file has content (INF doesn't flush by design -> flush explicitly before read)
    UniCoutLog::file_.flush();
    {
        std::ifstream fin(fname);
        ASSERT_TRUE(fin.good());
        std::string line;
        std::getline(fin, line);
        EXPECT_NE(line.find("hello file"), std::string::npos) << "REQ: log written to file";
    }

    // dumpAll_forUt restores to cout
    UniCoutLog::dumpAll_forUt();
    INF("hello cout again");
    ASSERT_GT(UniCoutLog::logLen(), 0u) << "REQ: can still log after restore";

    std::remove(fname.c_str());
}

TEST_F(UniCoutLogTest, setLogFileOK_emptyName_switchToCout)
{
    // first switch to file
    const std::string fname = "ut_log_empty_test.log";
    std::remove(fname.c_str());
    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));

    // empty name → switch back to cout
    ASSERT_TRUE(UniCoutLog::setLogFileOK("")) << "REQ: empty name = switch to cout";
    INF("back to cout");
    ASSERT_GT(UniCoutLog::logLen(), 0u) << "REQ: can log after switch to cout";

    std::remove(fname.c_str());
}

// - switch back to cout must release(flush+close) the previous log file handle,
//   else buffered INF data lingers unflushed & the OS fd leaks until next file switch
TEST_F(UniCoutLogTest, setLogFileOK_switchToCout_releasesFileHandle)
{
    const std::string fname = "ut_log_release_test.log";
    std::remove(fname.c_str());

    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));
    ASSERT_TRUE(UniCoutLog::file_.is_open()) << "REQ: file open after switch to file";

    ASSERT_TRUE(UniCoutLog::setLogFileOK("")) << "REQ: empty name = switch to cout";
    EXPECT_FALSE(UniCoutLog::file_.is_open())
        << "REQ: switch to cout flushes & closes the file (no dangling open handle)";

    std::remove(fname.c_str());
}
// - idempotent switch to cout while no file is open (default state) must be a safe no-op
TEST_F(UniCoutLogTest, setLogFileOK_switchToCout_whenAlreadyCout_noop)
{
    ASSERT_FALSE(UniCoutLog::file_.is_open()) << "precond: default state = cout, no file open";

    ASSERT_TRUE(UniCoutLog::setLogFileOK("")) << "REQ: switch to cout is OK even when already cout";
    EXPECT_FALSE(UniCoutLog::file_.is_open()) << "REQ: stays closed, no spurious open";

    INF("still cout");
    ASSERT_GT(UniCoutLog::logLen(), 0u) << "REQ: can still log after idempotent switch to cout";
}

TEST_F(UniCoutLogTest, setLogFileOK_badPath_fail)
{
    ASSERT_FALSE(UniCoutLog::setLogFileOK("/nonexistent_dir_12345/impossible.log"))
        << "REQ: bad path returns false";

    // should still log to cout (unchanged)
    INF("still cout");
    ASSERT_GT(UniCoutLog::logLen(), 0u) << "REQ: can still log after failed setLogFileOK";
}

// - end-to-end: TRC writes formatted trace to file, TRC-off produces nothing
TEST_F(UniCoutLogTest, TRC_endToEnd_fileOutput_and_traceOff)
{
    const std::string fname = "ut_trc_test.log";
    std::remove(fname.c_str());
    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));

    // TRC-on: trace appears in file
    TRC("event %s id=%d", "setPrev", 42);
    std::fflush(UniCoutLog::trcFp_);
    {
        std::ifstream fin(fname);
        std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        EXPECT_NE(content.find("event setPrev id=42"), std::string::npos)
            << "REQ: TRC-on writes formatted trace to file";
    }

    // TRC-off: no additional output
    const auto sizeBefore = std::ifstream(fname, std::ios::ate).tellg();
    traceOn_ = false;
    TRC("should not appear %d", 99);
    std::fflush(UniCoutLog::trcFp_);
    const auto sizeAfter = std::ifstream(fname, std::ios::ate).tellg();
    traceOn_ = true;
    EXPECT_EQ(sizeBefore, sizeAfter) << "REQ: TRC-off produces no output";

    std::remove(fname.c_str());
}

// - end-to-end: TRC safely truncates long messages (buf[256])
TEST_F(UniCoutLogTest, TRC_longMessage_truncatedToFile)
{
    const std::string fname = "ut_trc_trunc.log";
    std::remove(fname.c_str());
    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));

    // 300-char payload exceeds internal buf[256]
    const std::string longMsg(300, 'Z');
    TRC("%s", longMsg.c_str());
    std::fflush(UniCoutLog::trcFp_);

    std::ifstream fin(fname);
    std::string line;
    std::getline(fin, line);
    EXPECT_LE(line.size(), 255u) << "REQ: TRC truncates to buf size";
    EXPECT_NE(line.find("ZZZ"), std::string::npos) << "REQ: truncated content still present";

    std::remove(fname.c_str());
}

#define STRCOUTFSL
// ***********************************************************************************************
TEST(StrCoutFSLTest, BugFix_forceSave_thenDestructor_noDupOutput)
{
    // redirect cout to capture output
    std::ostringstream captured;
    auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
    {
        StrCoutFSL log;
        log.needLog();  // destructor will also save
        log << "Hello";
        log.forceSave();  // outputs "Hello\n"
        // destructor fires here — must NOT output "Hello" again
    }
    std::cout.rdbuf(oldBuf);  // restore cout

    // count occurrences of "Hello" in captured output
    EXPECT_EQ("Hello\n", captured.str()) << "REQ: forceSave()+destructor shall not duplicate output";
}

TEST(StrCoutFSLTest, forceSave_thenMoreWrites_outputAll)
{
    std::ostringstream captured;
    auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
    {
        StrCoutFSL log;
        log.needLog();
        log << "Part1";
        log.forceSave();  // outputs "Part1\n"
        log << "Part2";
        // destructor outputs "Part2\n" (only the new part)
    }
    std::cout.rdbuf(oldBuf);

    EXPECT_EQ("Part1\nPart2\n", captured.str()) << "REQ: forceSave() then more writes should output all";
}

}  // namespace rlib
