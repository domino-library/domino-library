/**
 * Copyright 2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - why: simplify multi UT cases' init
// ***********************************************************************************************
#pragma once

#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "UniLog.hpp"
#include "UniPtr.hpp"
#include "MsgSelf.hpp"
#include "ObjAnywhere.hpp"
#include "UtSoak.hpp"

#include "AsyncBack.hpp"
#include "Domino.hpp"
#include "DataDomino.hpp"
#include "WbasicDatDom.hpp"
#include "HdlrDomino.hpp"
#include "MultiHdlrDomino.hpp"
#include "PriDomino.hpp"
#include "FreeHdlrDomino.hpp"
#include "RmEvDom.hpp"
#include "ThPoolBack.hpp"

// ***********************************************************************************************
// UT req: combined domino shall pass all UT
#define DOMINO      (ObjAnywhere::getObj<MaxDom>      ().get())
#define NO_FREE_DOM (ObjAnywhere::getObj<MaxNofreeDom>().get())
#define PARA_DOM    (ObjAnywhere::getObj<TypeParam>   ().get())

using namespace testing;

namespace rlib
{
using MinHdlrDom  =                 HdlrDomino<Domino>;
using MinMhdlrDom = MultiHdlrDomino<MinHdlrDom>;
using MinPriDom   =       PriDomino<MinHdlrDom>;
using MinFreeDom  =  FreeHdlrDomino<MinHdlrDom>;

using MinDatDom =                                                              DataDomino<Domino>;
using MinWbasicDatDom =                                           WbasicDatDom<MinDatDom>;
using MaxNofreeDom = RmEvDom<PriDomino<MultiHdlrDomino<HdlrDomino<MinWbasicDatDom>>>>;  // diff order

using MinRmEvDom =                                                                         RmEvDom<Domino>;
using MaxDom = WbasicDatDom<MultiHdlrDomino<DataDomino<FreeHdlrDomino<PriDomino<HdlrDomino<MinRmEvDom>>>>>>;

// ***********************************************************************************************
template<class aT, class = void>
struct CanRmEV : std::false_type {};
template<class aT>
struct CanRmEV<aT, std::void_t<decltype(std::declval<aT>().rmEvOK(std::declval<const Domino::EvName&>()))>>
    : std::true_type {};

// ***********************************************************************************************
template<class aT, class = void>
struct CanRmAllHdlr : std::false_type {};
template<class aT>
struct CanRmAllHdlr<aT, std::void_t<decltype(std::declval<aT>().rmAllHdlr(std::declval<const Domino::EvName&>()))>>
    : std::true_type {};

// names this case passed to newEvent; absent key = first run, drop the whole graph
inline std::unordered_map<std::string, std::unordered_set<Domino::EvName>> s_caseEv_;
inline const Domino* s_noteDom_ = nullptr;
inline std::unordered_set<Domino::EvName>* s_noteSet_ = nullptr;

inline void noteNewEv_forUt(const Domino& dom, const Domino::EvName& name) noexcept
{
    if (&dom == s_noteDom_ && s_noteSet_)
        s_noteSet_->insert(name);
}
inline std::string curUtName_()
{
    const auto* info = UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// ***********************************************************************************************
// - simulate real world: init all objs via ObjAnywhere
// - all cases then use these objs
struct UtInitObjAnywhere : public UniLog, public Test
{
    UtInitObjAnywhere() : UniLog(UnitTest::GetInstance()->current_test_info()->name())
    {
        if (!ObjAnywhere::isInit())
        {
            ObjAnywhere::init(*this);

            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MsgSelf>(uniLogName()), *this))
                << "REQ: init MsgSelf";

            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<Domino>         (uniLogName()), *this))
                << "REQ: init Domino";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinDatDom>      (uniLogName()), *this))
                << "REQ: init MinDatDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinWbasicDatDom>(uniLogName()), *this))
                << "REQ: init MinWbasicDatDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinHdlrDom>     (uniLogName()), *this))
                << "REQ: init MinHdlrDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinMhdlrDom>    (uniLogName()), *this))
                << "REQ: init MinMhdlrDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinPriDom>      (uniLogName()), *this))
                << "REQ: init MinPriDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinFreeDom>     (uniLogName()), *this))
                << "REQ: init MinFreeDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MinRmEvDom>     (uniLogName()), *this))
                << "REQ: init MinRmEvDom";

            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MaxDom>         (uniLogName()), *this))
                << "REQ: init MaxDom";
            EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<MaxNofreeDom>   (uniLogName()), *this))
                << "REQ: init MaxNofreeDom";

            EXPECT_TRUE(ObjAnywhere::newObjOK<AsyncBack >()) << "REQ: init AsyncBack";
            EXPECT_TRUE(ObjAnywhere::newObjOK<ThPoolBack>()) << "REQ: init ThPoolBack";
        }

        // - example how main() callback MsgSelf to handle all msgs
        // - this lambda hides all impl details but a common interface = function<void()>
        pongMsgSelf_ = [msgSelf = MSG_SELF]{ if (msgSelf) msgSelf->handleAllMsg(); };
    }
    virtual void dumpIfFail()
    {
        if (!HasFailure()) return;
        std::cerr << "nMsg=" << (MSG_SELF ? MSG_SELF->nMsg() : 0) << '\n';
    }
    void TearDown() override
    {
        dumpIfFail();
        if (isSoak() && HasFailure()) soakReplayAndAbort();  // body fail; graph still intact
        // rm dummy hdlr that still in queue, may impact low-pri (1/ping-pong)
        if (MSG_SELF)
            while (MSG_SELF->nMsg()) MSG_SELF->handleAllMsg();  // low-pri: 1 msg per handleAllMsg
        dumpIfFail();
        if (isSoak() && HasFailure()) soakReplayAndAbort();
    }
    ~UtInitObjAnywhere()
    {
        GTEST_LOG_FAIL
        finishUniLogAfterCase(HasFailure());  // gmock Times is in derived dtor, before this
    }

    // -------------------------------------------------------------------------------------------
    MsgCB pongMsgSelf_;
};

// ***********************************************************************************************
// - gtest req template for dom cases (TYPED_TEST_)
template<class TypeParam>
struct UtParaDom : public UtInitObjAnywhere
{
    void dumpIfFail() override
    {
        if (!HasFailure()) return;
        if (PARA_DOM)
            for (auto&& en : PARA_DOM->evNames())
                std::cerr << en << '=' << PARA_DOM->state(en)
                    << " why=" << PARA_DOM->whyFalse(PARA_DOM->getEventBy(en)) << '\n';
        UtInitObjAnywhere::dumpIfFail();
    }
    void SetUp() override
    {
        if constexpr (CanRmEV<TypeParam>::value)
        {
            if (PARA_DOM)
            {
                const auto rec = s_caseEv_.find(curUtName_());
                if (rec == s_caseEv_.end())  // first run, drop the whole graph
                {
                    for (auto&& en : PARA_DOM->evNames())
                        EXPECT_TRUE(PARA_DOM->rmEvOK(en));
                }
                else  // not first run, drop only new evs - soak as real world
                {
                    for (auto&& en : rec->second)
                        (void)PARA_DOM->rmEvOK(en);
                }
                if (MSG_SELF)
                {
                    EXPECT_EQ(0u, MSG_SELF->nMsg()) << "REQ: no msg left to avoid CB invalid *this";
                }
                s_noteDom_ = PARA_DOM.get();
                s_noteSet_ = &s_caseEv_[curUtName_()];
                Domino::newEvHook_forUt = noteNewEv_forUt;
            }
        }
    }
    void TearDown() override
    {
        dumpIfFail();
        if (isSoak() && HasFailure()) soakReplayAndAbort();
        if (ObjAnywhere::isInit() && PARA_DOM != nullptr)
        {
            if constexpr (CanRmEV<TypeParam>::value)
            {
                Domino::newEvHook_forUt = nullptr;
                s_noteDom_ = nullptr;
                s_noteSet_ = nullptr;
                if constexpr (CanRmAllHdlr<TypeParam>::value)
                {
                    for (auto&& en : PARA_DOM->evNames())  // rm all hdlr in soak - avoid CB invalid *this
                        PARA_DOM->rmAllHdlr(en);
                }
            }
            else
            {
                EXPECT_TRUE(ObjAnywhere::emplaceObjOK<TypeParam>(nullptr, *this));
                EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<TypeParam>(uniLogName()), *this));
            }
        }
        UtInitObjAnywhere::TearDown();
    }
};

}  // namespace
