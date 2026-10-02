/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include "UniCoutLog.hpp"

#include <cstdarg>

using namespace std;

namespace rlib
{
// ***********************************************************************************************
UniCoutLog::Line UniCoutLog::oneLog() noexcept
{
    Line line;  // locks coutMutex_() before reading out_; lock lives until the caller's << chain ends
    try {
        ++nLogLine_;  // ut only
        line << "c[" << mt_timestamp() << ' ' << ULN_DEFAULT << '/';
    } catch(...) {}
    return line;
}

// ***********************************************************************************************
bool UniCoutLog::setLogFileOK(const string& aFileName) noexcept
{
    std::lock_guard<std::recursive_mutex> guard(coutMutex_());  // out_/file_ swap races with oneLog()
    try {
        if (aFileName.empty())
        {
            cout << "INF(UniCoutLog): switch to cout" << endl;
            out_ = &std::cout;
            resetTrcFp_();
            if (file_.is_open()) file_.close();  // safe: dump buf; no fd leak
            return true;
        }

        ofstream newFile(aFileName, ios::app);
        if (! newFile)
        {
            cout << "ERR(UniCoutLog): can't open log file " << aFileName << endl;
            return false;
        }
        FILE* newFp = std::fopen(aFileName.c_str(), "a");
        if (! newFp)
        {
            cout << "ERR(UniCoutLog): can't fopen log file " << aFileName << endl;
            return false;
        }

        cout << "INF(UniCoutLog): switch to log file " << aFileName << endl;
        file_ = std::move(newFile);
        out_ = &file_;
        resetTrcFp_(newFp);
        return true;
    } catch (...)
    {
        cout << "ERR(UniCoutLog): except=" << mt_exceptInfo() << " when open log file=" << aFileName << endl;
        return false;
    }
}

// ***********************************************************************************************
// - TRC(): mt_formatTRC (thread_local) + fwrite
// - MT safe : yes. C11 fwrite locks trcFp_. Does not take coutMutex_() (cout's lock);
//   sharing that lock would put TRC back on the slow path.
//   . fwrite to trcFp_ (FILE*): faster than ostream::write on 300K calls (507ms -> 260ms)
//   . trcFp_ tracks out_ via setLogFileOK(): stdout when cout, fopen'd when file
//   . setLogFileOK's fclose(old trcFp_) is not synchronized with an in-flight fwrite
void UniCoutLog::trcPrintf(const char* fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    auto [buf, n] = mt_formatTRC(fmt, ap);
    va_end(ap);
    std::fwrite(buf, 1, size_t(n), trcFp_);  // fwrite MT safe per C11; trcFp_ atomic
}

// ***********************************************************************************************
// - sync_with_stdio(false): cout and stdout use separate buffers.
//   . TRC stays on fwrite. If cout is tied to stdout, a flush makes stdout line-buffered
//     and TRC's fwrite slows about 4x. Untying them keeps that path fast.
//   . The stdio lock is gone with it, so concurrent cout << can crash. oneLog()'s coutMutex_()
//     covers a whole INF/WRN/ERR/HID line instead. TRC does not take coutMutex_().
static const bool kSyncOff = (std::ios::sync_with_stdio(false), true);

// ***********************************************************************************************
UniCoutLog              UniCoutLog::defaultUniLog_;
std::atomic<size_t>     UniCoutLog::nLogLine_ = 0;
std::ostream*           UniCoutLog::out_ = &std::cout;
std::ofstream           UniCoutLog::file_;
std::atomic<std::FILE*> UniCoutLog::trcFp_ = stdout;

}  // namespaces
