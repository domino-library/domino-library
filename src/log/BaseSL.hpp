/**
 * Copyright 2006-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// (SMART log: ONLY log for error transaction; NO log for succ transaction)
//
// - CONSTITUTION : base class of smart log,
//   * common & easy part of smart log is here [MUST-HAVE!]
//   * derivers to cover special & difficult part
//   . (tmp log can be fixed-len or dyn-len char[])
//   . (permanent log can be file or socket)
//
// - core : needLog_ (soul of smart log)
// ***********************************************************************************************
#pragma once

namespace rlib
{
// ***********************************************************************************************
class BaseSL  // SL = Smart Log
{
public:
    bool canDelLog() const noexcept { return not needLog_; }
    void needLog() noexcept { needLog_ = true; }

//------------------------------------------------------------------------------------------------
protected:    // derive use only
    virtual ~BaseSL() = default;

//------------------------------------------------------------------------------------------------
private:
    bool needLog_ = false;  // init=false is smartlog, =true is legacy(full log always)
};

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2006-07-24  CSZ       1)Created in 3G AMT feature
// 2010-01-18  CSZ       - base on aBUFFER_T & anOUTPUT_T
//                       - focus on must-have functionality of smart log
// 2010-01-27  CSZ       - separate construct/destruct the real buffer & output device to derivers
// 2011-02-22  CSZ       - cosmatic pervious version
//                       - giveup unify unformatted & formatted smartlog, too difficult
// 2011-04-28  CSZ       - unify unformatted & formatted smartlog together
// 2011-05-05  CSZ       - giveup unify different smart log together, use most direct way
// 2015-06-01  CSZ       - derive R_StrCountFSL_T by stringstream, so endl works
//                       - rename trigger() to needLog(), etc.
// 2015-06-04  CSZ       - derive R_StrFileFSL_T by stringstream
//                       - fix file open issue (at 30_IDs\smart.log under codeblock)
// 2020-10-26  CSZ       - StrCoutFSL for UT
//                       - align Domino coding style
// 2022-08-17  CSZ       - adjusted with UniLog
// ***********************************************************************************************
