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
ostream& UniCoutLog::oneLog() noexcept
{
    try {
        ++nLogLine_;  // ut only; no impact product's MT safe & mem safe
        *out_ << "c[" << mt_timestamp() << ' ' << ULN_DEFAULT << '/';
    } catch(...) {}
    return *out_;
}

// ***********************************************************************************************
bool UniCoutLog::setLogFileOK(const string& aFileName) noexcept
{
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
// - TRC(): mt_formatTRC (shared) + fwrite (C stdio)
//   . fwrite to trcFp_ (FILE*): faster than ostream::write on 300K calls (507ms -> 260ms)
//   . trcFp_ tracks out_ via setLogFileOK(): stdout when cout, fopen'd when file
void UniCoutLog::trcPrintf(const char* fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    auto [buf, n] = mt_formatTRC(fmt, ap);
    va_end(ap);
    std::fwrite(buf, 1, size_t(n), trcFp_);  // fwrite MT safe per C11; trcFp_ atomic
}

// ***********************************************************************************************
// - sync_with_stdio(false): decouple cout buffer from stdout (C FILE*) buffer
//   . prerequisite for TRC fwrite coexisting with INF/WRN/ERR cout<<
//   . without this: cout<<flush marks stdout line-buffered, TRC fwrite degrades 4x in suite
//   . interleave safety: both paths output complete lines (\n-terminated),
//     worst case = line-level reorder, never mid-line corruption;
static const bool kSyncOff = (std::ios::sync_with_stdio(false), true);

// ***********************************************************************************************
UniCoutLog              UniCoutLog::defaultUniLog_;
std::atomic<size_t>     UniCoutLog::nLogLine_ = 0;
std::ostream*           UniCoutLog::out_ = &std::cout;
std::ofstream           UniCoutLog::file_;
std::atomic<std::FILE*> UniCoutLog::trcFp_ = stdout;

}  // namespaces
