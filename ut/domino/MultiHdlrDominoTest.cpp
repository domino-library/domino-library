/**
 * Copyright 2020-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gmock/gmock.h>
#include <memory>  // for shared_ptr
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::set;
using std::string;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct MultiHdlrDominoTest : public UtParaDom<aParaDom>
{
    MOCK_METHOD(void, hdlr0, ());
    MOCK_METHOD(void, hdlr1, ());
    MOCK_METHOD(void, hdlr2, ());

    // -------------------------------------------------------------------------------------------
    MsgCB hdlr0_ = [this](){ this->hdlr0(); };
    MsgCB hdlr1_ = [this](){ this->hdlr1(); };
    MsgCB hdlr2_ = [this](){ this->hdlr2(); };

    set<Domino::Event> uniqueEVs_;
};
TYPED_TEST_SUITE_P(MultiHdlrDominoTest);

// ***********************************************************************************************
template<class aParaDom> using NofreeMultiHdlrDominoTest = MultiHdlrDominoTest<aParaDom>;  // for no-free testcase
TYPED_TEST_SUITE_P(NofreeMultiHdlrDominoTest);

#define ADD_AND_CALL
// ***********************************************************************************************
// add hdlr & normal call
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, GOLD_multiAddHdlr_ok)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");

    EXPECT_CALL(*this, hdlr0());  // req: legacy
    EXPECT_CALL(*this, hdlr1());  // req: multi-hdlr
    EXPECT_CALL(*this, hdlr2());  // req: multi-hdlr
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, aloneAddHdlr_ok)
{
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");  // req: alone add & call

    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, multiAddHdlr_bySameHdlrName_nok)
{
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr1_");  // req: same HdlrName -> fail
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2()).Times(0);
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
// ***********************************************************************************************
// special call hdlr
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, immediateCallback_ok)
{
    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->setState({{"event", true}});
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");  // req: immediate call
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeMultiHdlrDominoTest, repeatCallback_ok)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->setState({{"event", true}});  // req: samultaneous call
    this->pongMsgSelf_();

    PARA_DOM->setState({{"event", false}});
    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->setState({{"event", true}});  // req: repeat call
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, BugFix_invalidHdlr_noCrash)
{
    PARA_DOM->setHdlr("e1", nullptr);
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e1")) << "REQ: not create new Ev";

    PARA_DOM->multiHdlrOnSameEv("e1", nullptr, "e1 multi");
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e1")) << "REQ: not create new Ev";

    PARA_DOM->setState({{"e1", true}});  // req: no crash

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr1_, "e1 multi");
    EXPECT_CALL(*this, hdlr0());  // req: can add hdlr
    EXPECT_CALL(*this, hdlr1());  // req: can add hdlr
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
}

#define CHAIN
// ***********************************************************************************************
// chain satisfy -> hdlr
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, chain_callbackAllHdlr)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");
    PARA_DOM->setPrev("event", {{"prev", true}});

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setState({{"prev", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, newChain_immediateCallbackAll)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");
    PARA_DOM->setState({{"prev", true}});

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setPrev("event", {{"prev", true}});  // new chain
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, newChain_dupSatisfy_callbackOnce)
{
    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");
    PARA_DOM->setState({{"event", true}});  // 1st satisfy
    this->pongMsgSelf_();

    PARA_DOM->setState({{"prev", true}});
    PARA_DOM->setPrev("event", {{"prev", true}});  // 2nd satisfy
    EXPECT_EQ(0u, MSG_SELF->nMsg());
}
TYPED_TEST_P(MultiHdlrDominoTest, chain_sameHdlrOnDiffEvent_callbackEach)
{
    PARA_DOM->setPrev("event", {{"prev", true}});
    PARA_DOM->setPrev("prev", {{"prev prev", true}});
    PARA_DOM->setHdlr("prev", this->hdlr0_);
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("prev", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("prev", this->hdlr2_, "this->hdlr2_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");

    EXPECT_CALL(*this, hdlr0()).Times(2);
    EXPECT_CALL(*this, hdlr1()).Times(2);
    EXPECT_CALL(*this, hdlr2()).Times(2);
    PARA_DOM->setState({{"prev prev", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, multiPrev_onlyAllSatisfy_thenCallback)
{
    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1()).Times(3);

    PARA_DOM->setHdlr("event1", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event1", this->hdlr0_, "hn0_1");
    PARA_DOM->multiHdlrOnSameEv("event1", this->hdlr0_, "hn0_2");
    PARA_DOM->setPrev("event1", {{"prev1", true}});
    PARA_DOM->setPrev("event1", {{"prev2", false}});
    EXPECT_EQ(0u, MSG_SELF->nMsg());

    PARA_DOM->setHdlr("event2", this->hdlr1_);
    PARA_DOM->multiHdlrOnSameEv("event2", this->hdlr1_, "hn1_1");
    PARA_DOM->multiHdlrOnSameEv("event2", this->hdlr1_, "hn1_2");
    PARA_DOM->setPrev("event2", {{"prev1", true}});
    PARA_DOM->setPrev("event2", {{"prev2", true}});
    EXPECT_EQ(0u, MSG_SELF->nMsg());

    PARA_DOM->setState({{"prev1", true}, {"prev2", true}});
    this->pongMsgSelf_();
}

#define RM_HDLR
// ***********************************************************************************************
// rm hdlr, then F->T can't cb
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, rmHdlr_byHdlrName)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event", "this->hdlr1_")) << "REQ: rm MultiHdlrDomino";

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1()).Times(0);
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();

    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("event", "this->hdlr1_")) << "REQ: rm unexist hdlr";
    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("unexist ev", "this->hdlr1_")) << "REQ: rm unexist ev";
}
TYPED_TEST_P(MultiHdlrDominoTest, rmLegacyHdlr_byNoHdlrName)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event")) << "REQ: rm HdlrDomino that has no hdlr name";

    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
// - easier than name-by-name rm
// - HdlrDom need not since only 1 hdlr, simpler
// - not must-have so not GOLD_
TYPED_TEST_P(MultiHdlrDominoTest, rmHdlr_all)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");

    PARA_DOM->rmAllHdlr("event");
    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1()).Times(0);
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(MultiHdlrDominoTest, rmHdlr_subtree)
{
    PARA_DOM->setHdlr("/A", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("/A", this->hdlr1_, "this->hdlr1_");

    PARA_DOM->setHdlr("/A/B/C/D", this->hdlr2_);
    PARA_DOM->multiHdlrOnSameEv("/A/B/C/D", this->hdlr1_, "this->hdlr1_");

    auto&& evNames = PARA_DOM->evNames();
    for (auto&& evName : evNames)
    {
        auto&& it = evName.find("/A");
        if (it != string::npos) PARA_DOM->rmAllHdlr(evName);
    }
    EXPECT_EQ(0u, PARA_DOM->nHdlr("/A")) << "REQ: rm-ed";
    EXPECT_EQ(0u, PARA_DOM->nHdlr("/A/B/C/D")) << "REQ: rm-ed";
}
// ***********************************************************************************************
// rm on-road-hdlr
// ***********************************************************************************************
TYPED_TEST_P(NofreeMultiHdlrDominoTest, rmHdlrOnRoad)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr1_, "this->hdlr1_");
    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr2_, "this->hdlr2_");
    PARA_DOM->setState({{"event", true}});  // 3 cb on road
    EXPECT_TRUE(MSG_SELF->nMsg());
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event")) << "REQ: invalidate HdlrDom";
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event", "this->hdlr2_")) << "REQ: invalidate MultiDom";

    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2()).Times(0);
    this->pongMsgSelf_();

    PARA_DOM->setState({{"event", false}});
    PARA_DOM->setState({{"event", true}});  // retrigger

    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2()).Times(0);
    this->pongMsgSelf_();
}
// ***********************************************************************************************
// rm invalid
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, rmHdlr_invalid)
{
    PARA_DOM->newEvent("event");
    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("event", "invalid hdlr")) << "REQ: invalid hdlr name";

    //EXPECT_FALSE(PARA_DOM->rmOneHdlrOK(Domino::D_EVENT_FAILED_RET, nullptr)) << "REQ: invalid ev";

    PARA_DOM->multiHdlrOnSameEv("event", this->hdlr0_, "h0");  // inc cov
    //EXPECT_FALSE(PARA_DOM->rmOneHdlrOK(ev, nullptr)) << "REQ: rm null hdlr";
}

#define FORCE_CALL
// ***********************************************************************************************
//  call all hdlr(s) since via effect_(), state F->T also all hdlr(s))
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, GOLD_force_call)
{
    EXPECT_CALL(*this, hdlr0()).Times(0);  // req: no call
    EXPECT_CALL(*this, hdlr1()).Times(0);  // req: no call
    PARA_DOM->forceAllHdlr("e1");
    EXPECT_EQ(0u, MSG_SELF->nMsg());

    PARA_DOM->setHdlr("e1", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr1_, "this->hdlr1_");
    EXPECT_CALL(*this, hdlr0());  // req: force call
    EXPECT_CALL(*this, hdlr1());  // req: force call
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeMultiHdlrDominoTest, repeat_force_call)
{
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr1_, "this->hdlr1_");
    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0());  // req: repeat force call
    EXPECT_CALL(*this, hdlr1());  // req: repeat force call
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0()).Times(0);  // req: no call
    EXPECT_CALL(*this, hdlr1()).Times(0);  // req: no call
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1")) << "REQ: rm hdlr";
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1", "this->hdlr1_")) << "REQ: rm hdlr";
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();
}

#define N_HDLR
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, n_hdlr)
{
    EXPECT_EQ(0u, PARA_DOM->nHdlr("e1")) << "REQ: init no hdlr";

    PARA_DOM->setHdlr("e1", this->hdlr0_);
    EXPECT_EQ(1u, PARA_DOM->nHdlr("e1")) << "REQ: after added";

    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr1_, "this->hdlr1_");
    EXPECT_EQ(2u, PARA_DOM->nHdlr("e1")) << "REQ: after added";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1")) << "REQ: rm hdlr";
    EXPECT_EQ(1u, PARA_DOM->nHdlr("e1")) << "REQ: after del";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1", "this->hdlr1_")) << "REQ: rm hdlr";
    EXPECT_EQ(0u, PARA_DOM->nHdlr("e1")) << "REQ: after del";
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(MultiHdlrDominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr0_, "h0");
    PARA_DOM->multiHdlrOnSameEv("e1", this->hdlr1_, "h1");
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e1"));
    EXPECT_EQ(1u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e1"));

    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);
    EXPECT_EQ(2u, this->uniqueEVs_.size());

    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("e2", "h2")) << "REQ: rm nonexist hdlr";  // shall NOT generate new event
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e2"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());

    PARA_DOM->nHdlr("e3");  // shall NOT generate new event
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e3"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());

    PARA_DOM->rmAllHdlr("e4");  // shall NOT generate new event
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e4"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(MultiHdlrDominoTest
    , GOLD_multiAddHdlr_ok
    , aloneAddHdlr_ok
    , multiAddHdlr_bySameHdlrName_nok
    , immediateCallback_ok
    , BugFix_invalidHdlr_noCrash

    , chain_callbackAllHdlr
    , newChain_immediateCallbackAll
    , newChain_dupSatisfy_callbackOnce
    , chain_sameHdlrOnDiffEvent_callbackEach
    , multiPrev_onlyAllSatisfy_thenCallback

    , rmHdlr_byHdlrName
    , rmLegacyHdlr_byNoHdlrName
    , rmHdlr_all
    , rmHdlr_invalid
    , rmHdlr_subtree

    , GOLD_force_call

    , n_hdlr

    , nonConstInterface_shall_createUnExistEvent_withStateFalse
);
using AnyMultiHdlrDom = Types<MinMhdlrDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, MultiHdlrDominoTest, AnyMultiHdlrDom);

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(NofreeMultiHdlrDominoTest
    , repeatCallback_ok

    , rmHdlrOnRoad

    , repeat_force_call
);
using AnyNofreeMultiHdlrDom = Types<MinMhdlrDom, MaxNofreeDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, NofreeMultiHdlrDominoTest, AnyNofreeMultiHdlrDom);
}  // namespace
