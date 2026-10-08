/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include "UniCoutLog.hpp"

#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

namespace
{
// one FILE* for the process. Never swapped. Not closed here: a TRC during static
// destruction still has a live stream, and libc flushes it after that.
// Switching the destination is dup2 under flockfile (the same lock fwrite takes).
std::FILE* openTrcFile_() noexcept
{
    const int fd = ::dup(STDOUT_FILENO);
    if (fd < 0) return nullptr;
    std::FILE* fp = ::fdopen(fd, "a");
    if (fp == nullptr) ::close(fd);
    return fp;
}

std::FILE* trcFile_() noexcept
{
    static std::FILE* const fp = openTrcFile_();
    return fp;
}
}  // namespace

namespace rlib
{

// ***********************************************************************************************
bool UniCoutLog::redirectTrcOK_(const char* aPath) noexcept
{
    std::FILE* fp = trcFile_();
    if (fp == nullptr) return false;

    const int fd = (aPath == nullptr)
        ? ::dup(STDOUT_FILENO)
        : ::open(aPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return false;

    flockfile(fp);  // glibc: recursive, so fflush below does not deadlock; fwrite waits on this lock
    std::fflush(fp);
    const bool ok = ::dup2(fd, fileno(fp)) >= 0;  // success returns the fd, not 0
    if (ok)
    {
        // glibc accepts setvbuf after fflush. A tty stays line-buffered; a file is fully buffered.
        const int mode = (aPath == nullptr && ::isatty(fd)) ? _IOLBF : _IOFBF;
        (void)std::setvbuf(fp, nullptr, mode, BUFSIZ);
    }
    funlockfile(fp);
    ::close(fd);
    return ok;
}

void UniCoutLog::flushTrc_() noexcept
{
    if (std::FILE* fp = trcFile_()) std::fflush(fp);
}

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
            if (! redirectTrcOK_(nullptr))
            {
                cout << "ERR(UniCoutLog): can't restore TRC to stdout" << endl;
                return false;
            }
            cout << "INF(UniCoutLog): switch to cout" << endl;
            out_ = &std::cout;
            if (file_.is_open()) file_.close();  // safe: dump buf; no fd leak
            return true;
        }

        ofstream newFile(aFileName, ios::app);
        if (! newFile || ! redirectTrcOK_(aFileName.c_str()))  // redirect not called when newFile failed
        {
            cout << "ERR(UniCoutLog): can't open log file " << aFileName << endl;
            return false;
        }

        cout << "INF(UniCoutLog): switch to log file " << aFileName << endl;
        file_ = std::move(newFile);
        out_ = &file_;
        return true;
    } catch (...)
    {
        cout << "ERR(UniCoutLog): except=" << mt_exceptInfo() << " when open log file=" << aFileName << endl;
        return false;
    }
}

// ***********************************************************************************************
// - TRC(): mt_formatTRC (thread_local) + fwrite to the private FILE*
// - MT safe : yes. Does not take coutMutex_() (that lock would put TRC back on the slow path).
//   . fwrite: faster than ostream::write on 300K calls (507ms -> 260ms)
//   . FILE* is stable. setLogFileOK dup2's under flockfile, which fwrite also takes.
void UniCoutLog::trcPrintf(const char* fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    auto [buf, n] = mt_formatTRC(fmt, ap);
    va_end(ap);
    if (std::FILE* fp = trcFile_()) std::fwrite(buf, 1, size_t(n), fp);
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

}  // namespaces
