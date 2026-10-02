/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#define WITH_HID_LOG 1  // this TU only: HID is off unless a build asks for debug clues
#include "UniCoutLog.hpp"

#include "StrCoutFSL.hpp"

#include <atomic>
#include <initializer_list>
#include <sstream>
#include <thread>

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

TEST_F(UniCoutLogTest, soak_dual_forceSave_flushes)
{
    UniCoutLog::dropAllBuf_forUt();    // SmartLog dual; cout has no buf
    UniCoutLog::forceSaveAll_forUt();  // flush cout + trc FILE*; abort() keeps neither
    UniCoutLog::rawDumpAll_forUt();    // Die() must not touch cout's buf
}

#define MT_SAFE
// ***********************************************************************************************
// swap cout under coutMutex_(): another thread may still be logging
struct CoutCapture
{
    std::ostringstream buf;
    std::streambuf* old;
    CoutCapture()
    {
        std::lock_guard<std::recursive_mutex> guard(coutMutex_());
        old = std::cout.rdbuf(buf.rdbuf());
    }
    ~CoutCapture()
    {
        std::lock_guard<std::recursive_mutex> guard(coutMutex_());
        std::cout.rdbuf(old);
    }
};

static bool endsWith(const std::string& aLine, const char* aMark)
{
    const auto n = std::char_traits<char>::length(aMark);
    return aLine.size() >= n && aLine.compare(aLine.size() - n, n, aMark) == 0;
}

// a whole line starts with one prefix and ends with one mark.
// catching "c[" only inside the line misses a full line inserted into another line's prefix.
static void expectWholeLines(const std::string& aText, std::initializer_list<const char*> aMarks)
{
    std::istringstream in(aText);
    std::string line;
    while (std::getline(in, line))
    {
        const bool isInf = line.rfind("c[", 0) == 0;
        const bool isHid = line.rfind("cout[", 0) == 0;
        EXPECT_TRUE(isInf || isHid) << "REQ: line starts with a prefix, got: " << line;
        EXPECT_EQ(line.find("c[", 1), std::string::npos) << "REQ: one prefix per line, got: " << line;
        size_t nMark = 0;
        for (auto m : aMarks)
            nMark += endsWith(line, m);
        EXPECT_EQ(nMark, 1u) << "REQ: line ends with one mark, got: " << line;
    }
}

// - two threads INF and one thread HID, all on cout. sync_with_stdio(false) drops cout's own lock.
TEST_F(UniCoutLogTest, infAndHid_threads_eachLineWhole)
{
    CoutCapture cap;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    auto runInf = [&](const char* aMark)
    {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();  // bare spin starves the peer under valgrind
        for (int i = 0; i < 200; ++i)
            INF(aMark);
    };
    auto runHid = [&]
    {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();
        for (int i = 0; i < 200; ++i)
            HID("MARK-H");
    };
    std::thread a(runInf, "MARK-A");
    std::thread b(runInf, "MARK-B");
    std::thread h(runHid);
    while (ready.load() < 3) std::this_thread::yield();
    go.store(true);
    a.join();
    b.join();
    h.join();
    std::cout.flush();
    const auto text = cap.buf.str();
    EXPECT_NE(text.find("MARK-A"), std::string::npos) << "REQ: thread A wrote";
    EXPECT_NE(text.find("MARK-B"), std::string::npos) << "REQ: thread B wrote";
    EXPECT_NE(text.find("MARK-H"), std::string::npos) << "REQ: HID wrote";
    expectWholeLines(text, {"MARK-A", "MARK-B", "MARK-H"});
}

// - SafePtr::operator-> calls HID, and that call can sit inside an INF argument
TEST_F(UniCoutLogTest, hid_insideInfArgument_returns)
{
    CoutCapture cap;
    bool ran = false;
    auto hidThen = [&]()
    {
        HID("MARK-NEST");
        ran = true;
        return 1;
    };
    INF("n=" << hidThen());
    std::cout.flush();
    EXPECT_TRUE(ran) << "REQ: HID inside an INF argument returns";
    EXPECT_NE(cap.buf.str().find("MARK-NEST"), std::string::npos) << "REQ: that HID was not compiled out";
}

// - one thread INF while another switches out_ between cout and a file
// - yield is outside coutMutex_(), so sw can take the lock under valgrind
TEST_F(UniCoutLogTest, setLogFileOK_whileInf_fileLinesWhole)
{
    const std::string fname = "ut_log_switch_race.log";
    std::remove(fname.c_str());
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::thread inf([&]
    {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();
        while (!stop.load())
        {
            INF("MARK-F");
            std::this_thread::yield();
        }
    });
    std::thread sw([&]
    {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();
        for (int i = 0; i < 20; ++i)
        {
            EXPECT_TRUE(UniCoutLog::setLogFileOK(fname));
            std::this_thread::yield();  // INF writes while out_ is the file
            EXPECT_TRUE(UniCoutLog::setLogFileOK(""));
            std::this_thread::yield();
        }
        stop.store(true);
    });
    while (ready.load() < 2) std::this_thread::yield();
    go.store(true);
    inf.join();
    sw.join();
    EXPECT_TRUE(UniCoutLog::setLogFileOK("")) << "REQ: back to cout, file flushed";

    std::ifstream fin(fname);
    const std::string text((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("MARK-F"), std::string::npos) << "REQ: some INF landed in the file";
    expectWholeLines(text, {"MARK-F"});
    std::remove(fname.c_str());
}

// - TRC is fwrite to the log file, not cout, so a cout capture would miss it
static void expectWholeTrcLines(const std::string& aText)
{
    std::istringstream in(aText);
    std::string line;
    int nLine = 0;
    while (std::getline(in, line))
    {
        ++nLine;
        EXPECT_TRUE(!line.empty() && line[0] >= '0' && line[0] <= '9')
            << "REQ: TRC line starts with a timestamp, got: " << line;
        const auto pos = line.rfind("MARK-T");
        EXPECT_NE(pos, std::string::npos) << "REQ: TRC line has a mark, got: " << line;
        if (pos == std::string::npos)
            continue;
        EXPECT_EQ(line.find("MARK-T"), pos) << "REQ: one TRC mark per line, got: " << line;
        EXPECT_LT(pos + 6, line.size()) << "REQ: mark has a thread id, got: " << line;
        for (size_t i = pos + 6; i < line.size(); ++i)
            EXPECT_TRUE(line[i] >= '0' && line[i] <= '9') << "REQ: line ends at the mark, got: " << line;
    }
    EXPECT_GT(nLine, 0) << "REQ: TRC wrote to the file";
}

TEST_F(UniCoutLogTest, trc_threads_eachLineWhole)
{
    const std::string fname = "ut_trc_race.log";
    std::remove(fname.c_str());
    ASSERT_TRUE(UniCoutLog::setLogFileOK(fname));

    constexpr int nThread = 4;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::thread th[nThread];
    for (int t = 0; t < nThread; ++t)
    {
        th[t] = std::thread([&, t]
        {
            const auto mark = "MARK-T" + std::to_string(t);
            ready.fetch_add(1);
            while (!go.load()) std::this_thread::yield();
            for (int i = 0; i < 50; ++i)
                TRC("%s", mark.c_str());
        });
    }
    while (ready.load() < nThread) std::this_thread::yield();
    go.store(true);
    for (auto& t : th)
        t.join();
    std::fflush(UniCoutLog::trcFp_);
    EXPECT_TRUE(UniCoutLog::setLogFileOK(""));

    std::ifstream fin(fname);
    const std::string text((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    expectWholeTrcLines(text);
    std::remove(fname.c_str());
}

}  // namespace rlib
