/**
 * Copyright 2006 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// CONSTITUTION : 1 solution of smart log,
// - tmp log is std::string via lightweight custom streambuf (dyn buf)
// - permanent log is cout (screen)
// . shortcoming: multiple smart logs will mix output on screen
// - formatted log (FSL = Formatted Smart Log)
// . slower than unformatted smart log, but more readable
// - derive from BaseSL is to reuse common func
//
// CORE: sbuf_ (func around it)
//
// MT safe: false
//
// mem safe: yes
// ***********************************************************************************************
#pragma once

#include <iostream>
#include <string>

#include "BaseSL.hpp"

namespace rlib
{
// ***********************************************************************************************
// - lightweight streambuf that appends directly to std::string
// - avoids std::stringbuf's bidirectional buffer & internal double-buffering overhead
class StringStreamBuf : public std::streambuf
{
public:
    const std::string& str() const noexcept { return buf_; }
    size_t size() const noexcept { return buf_.size(); }
    void clear() noexcept { buf_.clear(); }

protected:
    int_type overflow(int_type ch) override
    {
        if (ch != traits_type::eof())
            buf_ += static_cast<char>(ch);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        if (!s || n <= 0) return 0;
        buf_.append(s, static_cast<size_t>(n));
        return n;
    }

private:
    std::string buf_;
};

// ***********************************************************************************************
// - holder to ensure sbuf_ is constructed before std::ostream base
struct StrCoutFSL_BufHolder
{
    StringStreamBuf sbuf_;
};

// ***********************************************************************************************
class StrCoutFSL  // FSL = Formatted Smart Log
    : public BaseSL
    , private StrCoutFSL_BufHolder  // must be before std::ostream to init sbuf_ first
    , public std::ostream  // lightweight: no bidirectional stringbuf overhead
{
public :
    StrCoutFSL() : std::ostream(&sbuf_) {}
    ~StrCoutFSL() noexcept;

    void forceSave() noexcept;
    void dropBuf() noexcept { sbuf_.clear(); }  // discard text; keep the log object
    size_t size() const noexcept { return sbuf_.size(); }
};

// ***********************************************************************************************
inline
StrCoutFSL::~StrCoutFSL() noexcept
{
    if (canDelLog())
        return;

    forceSave();
}

// ***********************************************************************************************
inline
void StrCoutFSL::forceSave() noexcept
{
    if (sbuf_.size() > 0) {
        std::cout << sbuf_.str() << '\n';
        sbuf_.clear();
    }
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2006-       CSZ       - create
// 2022-01-01  PJ & CSZ  - formal log & naming
// 2026-03-05  CSZ       - replace stringstream with lightweight custom streambuf + std::string
// 2026-09-20  CSZ       - dropBuf() for soak to discard success text
// ***********************************************************************************************
