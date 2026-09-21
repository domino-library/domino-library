/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include "UniSmartLog.hpp"

#include <cstdarg>

using namespace std;

namespace rlib
{
// ***********************************************************************************************
UniSmartLog::UniSmartLog(const LogName& aUniLogName) noexcept : uniLogName_(aUniLogName)
{
    // for(auto&& name_log : name_log_S_) cout << name_log.first<<", p=" << name_log.second.get() << endl;
    auto [name_log, insertNew] = name_log_S_.try_emplace(aUniLogName);
    if (insertNew)
    {
        smartLog_ = make_shared<SmartLog>();
        name_log->second = smartLog_;
        HID("creatd new log=" << (void*)(smartLog_.get()) << ", name=" << aUniLogName << ", nLog=" << nLog());
    }
    else
    {
        smartLog_ = name_log->second;
        HID("reused existing log=" << (void*)(smartLog_.get()) << ", name=" << aUniLogName << ", nLog=" << nLog());
    }
}

// ***********************************************************************************************
SmartLog& UniSmartLog::oneLog() const noexcept
{
    try { *smartLog_ << "s[" << mt_timestamp() << ' ' << uniLogName_ << '/'; } catch(...) {}
    return *smartLog_;
}

// ***********************************************************************************************
// - TRC fast-path impl (smart-log backend): mt_formatTRC (shared) + ostream::write
// - MT-unsafe ok: UniSmartLog is single-threaded by design (see UniSmartLog.hpp note)
void UniSmartLog::trcPrintf(const char* fmt, ...) noexcept
{
    va_list ap;
    va_start(ap, fmt);
    auto [buf, n] = mt_formatTRC(fmt, ap);
    va_end(ap);
    try { defaultUniLog_.trcOut().write(buf, n); } catch (...) {}
}

// ***********************************************************************************************
LogStore    UniSmartLog::name_log_S_;
UniSmartLog UniSmartLog::defaultUniLog_;

}  // namespaces
