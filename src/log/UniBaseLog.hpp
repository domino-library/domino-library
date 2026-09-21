/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - Issue/why:
//   . all common of eg UniSmartLog, UniCoutLog
// ***********************************************************************************************
#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <string>
#include <thread>

using LogName = std::string;

// ***********************************************************************************************
// - mem safe: yes
// - MT safe : yes of UniCoutLog, no of UniSmartLog
#ifdef __FILE_NAME__  // g++12/c++20
#define BUF(content) __FILE_NAME__ << ':' << __func__ << "():" << __LINE__ << ": " << content << '\n'
#else
#define BUF(content)                         __func__ << "():" << __LINE__ << ": " << content << '\n'
#endif

// ***********************************************************************************************
// flush policy:
// - ERR: flush (to avoid loss)
// - WRN: flush (to avoid loss)
// - INF: NO flush (rely on stream buffer + atexit-flush; ~50% perf gain w/o flush)
// - TRC: NO flush (most perf)
// - do-while: avoid dangling-else; allow tmp var, break/etc; same performance as {} block
#define INF(content) do{ oneLog() << "INF/" << std::this_thread::get_id() << "] " << BUF(content);               }while(0)
#define WRN(content) do{ oneLog() << "WRN/" << std::this_thread::get_id() << "] " << BUF(content) << std::flush; }while(0)
#define ERR(content) do{ oneLog() << "ERR/" << std::this_thread::get_id() << "] " << BUF(content) << std::flush; }while(0)

// ***********************************************************************************************
// - TRC/trace: hot-path event log for eg visualization (log -> Excel Gantt)
//   . -13% perf gain: TRC(snprintf) vs INF(6-10 ostream operator<<)
//   . -19% perf gain: TRC(fwrite) vs INF(out_->write())
//     type-safe composable formatting that printf can't match
// - runtime switch: env var TRACE_OFF=1 disables TRC for pure-computation profiling
namespace rlib { inline bool traceOn_ = (std::getenv("TRACE_OFF") == nullptr); }
#define TRC(...) ((void)0)  // dummy, will be replaced by real

// ***********************************************************************************************
// - HID() is to be more debug but product code shall disable them
// - HID() uses cout since UniSmartLog is NOT MT safe
//   . under single thread, can change cout back to oneLog() for smart log
// - HID() is MT safe also upon UniSmartLog
#if WITH_HID_LOG
#define HID(content) { std::cout << "cout[" << rlib::mt_timestamp() << "/HID/" \
    << std::this_thread::get_id() << "] " << BUF(content) << std::dec << std::flush; }
#else
#define HID(content) {}
#endif

#define GTEST_LOG_FAIL { if (Test::HasFailure()) needLog(); }  // UT only

namespace rlib
{
// ***********************************************************************************************
// - MT safe : yes (aStr is const ref)
// - mem safe: yes
inline void cout_ascii(const std::string& aStr) noexcept
{
    try {  // cout may except(eg ios_base::failure) though rare; try-catch is safest as a log
        std::string buf;
        buf.reserve(aStr.size() * 2);
        unsigned char preC = 0;
        for (unsigned char c : aStr)
        {
            if (std::isprint(c))
            {
                if (preC == 0xd || preC == 0xa)
                {
                    buf += '\n';
                    preC = 0;
                }
                buf += static_cast<char>(c);
            }
            else
            {
                char hex[16];
                snprintf(hex, sizeof(hex), "[`%zx'}", static_cast<size_t>(c));
                buf += hex;
                preC = c;
            }
        }
        std::cout << buf << std::dec << std::endl;
    }
    catch(...) {}
}

// ***********************************************************************************************
// - must call inside catch block
// - MT safe : yes
// - mem safe: yes (ret valid until outer catch exits)
inline const char* mt_exceptInfo() noexcept
{
    try { throw; }
    catch(const std::exception& e) { return e.what(); }
    catch(...) { return "unknown"; }
}

// ***********************************************************************************************
// - MT safe : yes
// - mem safe: yes
inline const char* mt_timestamp() noexcept
{
    static thread_local char buf[] = "ddd/HH:MM:SS.123456";  // thread_local is MT safe
    static thread_local std::time_t cachedSec_ = 0;  // same-sec cache (-15%): skip localtime_r+strftime

    const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto curSec = static_cast<std::time_t>(nowUs / 1'000'000);
    if (curSec != cachedSec_)  // rare: once per second
    {
        cachedSec_ = curSec;
        struct tm now_tm;
        strftime(buf, sizeof(buf), "%j/%T.", localtime_r(&curSec, &now_tm));  // cpplint asks localtime_r (MT safe)
    }
    snprintf(buf + sizeof(buf) - 7, 7, "%06u", unsigned(nowUs % 1'000'000));  // just refresh .123456
    return buf;
}

// ***********************************************************************************************
// - format TRC: "timestamp msg\n" into thread_local buf
// - MT safe : yes
// - mem safe: yes (ret valid until next call from same thread)
struct TrcBuf { const char* data; int len; };
inline TrcBuf mt_formatTRC(const char* aFormat, va_list ap) noexcept
{
    static thread_local char buf[256];
    const int ntsp = snprintf(buf, sizeof(buf), "%s ", mt_timestamp());
    int nmsg = vsnprintf(buf + ntsp, sizeof(buf) - ntsp - 1, aFormat, ap);  // -1 for '\n'
    if (nmsg < 0) nmsg = 0;
    int n = ntsp + nmsg;
    if (n >= int(sizeof(buf)) - 1) n = sizeof(buf) - 2;
    buf[n++] = '\n';
    return {buf, n};
}

constexpr char ULN_DEFAULT[] = "DEFAULT";
}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2022-08-29  CSZ       1)create
// 2023-05-29  CSZ       - ms/us in mt_timestamp
// 2024-02-22  CSZ       2)mem-safe
// 2025-04-07  CSZ       3)tolerate exception; MT safe
// 2026-04-17  CSZ       4)TRC; perf opt other logs
// ***********************************************************************************************
