/**
 * Copyright 2020 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gmock/gmock.h>
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::set;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct HdlrDominoTest : public UtParaDom<aParaDom>
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
TYPED_TEST_SUITE_P(HdlrDominoTest);

// ***********************************************************************************************
template<class aParaDom> using NofreeHdlrDominoTest = HdlrDominoTest<aParaDom>;  // for no-free testcase
TYPED_TEST_SUITE_P(NofreeHdlrDominoTest);

#define ADD_AND_CALL
// ***********************************************************************************************
// add & call hdlr
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, GOLD_add_and_call)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);  // req: add hdlr
    EXPECT_CALL(*this, hdlr0());
    PARA_DOM->setState({{"event", true}});  // req: F->T trigger the call
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr1()).Times(0);  // REQ: T->T no call (only F->T is trigger)
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr1()).Times(0);  // REQ: T->F no call
    PARA_DOM->setState({{"event", false}});
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr1()).Times(0);  // REQ: F->F no call
    PARA_DOM->setState({{"event", false}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, immediate_call)
{
    PARA_DOM->setState({{"event", true}});
    EXPECT_CALL(*this, hdlr0());
    PARA_DOM->setHdlr("event", this->hdlr0_);  // req: immediate call since already T from default (always=F)
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, UC_reTrigger_reCall)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    EXPECT_CALL(*this, hdlr0()).Times(2);
    PARA_DOM->setState({{"event", true}});  // 1st trigger
    PARA_DOM->setState({{"event", false}});
    PARA_DOM->setState({{"event", true}});  // 2nd trigger
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, except_hdlr)
{
    int step = 0;
    PARA_DOM->setHdlr("event", [&step](){
        step = 1;
        throw std::runtime_error("hdlr except");
        step = 2;
    });
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(1, step) << "REQ: HdlrDom shall tolerate except hdlr";
}

#define CHAIN
// ***********************************************************************************************
// chain satisfy -> hdlr
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, GOLD_trigger_chain_call)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);      // downstream
    PARA_DOM->setPrev("event", {{"prev", true}});  // upstream

    EXPECT_CALL(*this, hdlr0());  // REQ: upstream trigger downstream call
    PARA_DOM->setState({{"prev", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, immediate_chain_call)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setState({{"prev", true}});
    EXPECT_CALL(*this, hdlr0());  // REQ: immediate call
    PARA_DOM->setPrev("event", {{"prev", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, GOLD_trigger_chain_many_calls)
{
    PARA_DOM->setHdlr("e0", this->hdlr0_);
    PARA_DOM->setLinkedHdlr("e1", this->hdlr1_, "e0");
    PARA_DOM->setLinkedHdlr("e2", this->hdlr2_, "e1");

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setState({{"e0", true}});  // T->T->T, req: trigger all calls
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setState({{"e0", false}});  // F->F->F
    PARA_DOM->setState({{"e0", true}});   // T->T->T, req: trigger all calls
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, wrongOrderPrev_wrongCallback)
{
    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no callback
    PARA_DOM->setHdlr("event with simu prev setting", this->hdlr0_);
    PARA_DOM->setPrev("event with simu prev setting", {{"prev1", true}, {"prev2", false}});  // simu prev
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0());  // REQ: wrong callback
    PARA_DOM->setHdlr("event with wrong prev setting", this->hdlr0_);
    PARA_DOM->setPrev("event with wrong prev setting", {{"prev2", false}});  // wrong order
    PARA_DOM->setPrev("event with wrong prev setting", {{"prev1", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, GOLD_safeUpgrade_allPrevBeforeState)
{
    // REQ: eNB-upgrade-like safe pattern — build entire dependency graph first, then trigger
    //   download_ok ──(T)──┐
    //   version_ok ──(T)──├── install_ready ──(T)── install_done
    //   no_abort   ──(F)──┘
    //   (3 independent heads → 1 gate → 1 action)
    PARA_DOM->setHdlr("install_ready", this->hdlr0_);
    PARA_DOM->setPrev("install_ready", {{"download_ok", true}, {"version_ok", true}, {"no_abort", false}});
    PARA_DOM->setPrev("install_done", {{"install_ready", true}});

    // REQ: no premature trigger — none of the prerequisites are met yet
    EXPECT_CALL(*this, hdlr0()).Times(0);
    this->pongMsgSelf_();

    // partial satisfaction — still no trigger
    PARA_DOM->setState({{"download_ok", true}});
    EXPECT_CALL(*this, hdlr0()).Times(0);
    this->pongMsgSelf_();

    // all satisfied → trigger
    PARA_DOM->setState({{"version_ok", true}});
    EXPECT_CALL(*this, hdlr0());
    this->pongMsgSelf_();
    EXPECT_TRUE(PARA_DOM->state("install_done")) << "REQ: downstream propagated";

    // diagnose: intentionally break by abort → should propagate F
    PARA_DOM->setState({{"no_abort", true}});  // abort happened!
    EXPECT_FALSE(PARA_DOM->state("install_ready"));
    EXPECT_FALSE(PARA_DOM->state("install_done")) << "REQ: abort propagated to end";
    EXPECT_EQ("no_abort==true", PARA_DOM->whyFalse(PARA_DOM->getEventBy("install_ready")))
        << "REQ: whyFalse diagnoses the root cause";
}

#define MULTI_HDLR  // multi hdlr by alias event
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, multiHdlr_onOneEvent_nok)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setHdlr("event", this->hdlr1_);  // req: refuse

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1()).Times(0);
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, GOLD_multiHdlr_onDiffEvent_ok)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setLinkedHdlr("alias event", this->hdlr1_, "event");  // req: accept on alias ev
    PARA_DOM->setLinkedHdlr("alias-2", this->hdlr2_, "event");      // req: accept on diff alias

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2());
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, multiHdlr_onOneAliasEvent_nok)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setLinkedHdlr("alias event", this->hdlr1_, "event");
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setLinkedHdlr("alias event", this->hdlr2_, "event"))
        << "REQ: refuse overwrite hdlr";
    PARA_DOM->newEvent("create alias");
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setLinkedHdlr("create alias", this->hdlr2_, "event"))
        << "REQ: refuse existing ev as alias to avoid handling complex scenario";

    EXPECT_CALL(*this, hdlr0());
    EXPECT_CALL(*this, hdlr1());
    EXPECT_CALL(*this, hdlr2()).Times(0);
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, setLinkedHdlr_selfLoop_failure_is_atomic)
{
    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET,
        PARA_DOM->setLinkedHdlr("same", this->hdlr0_, "same"));

    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("same"))
        << "REQ: failed setLinkedHdlr() leaves no event";
    EXPECT_EQ(0u, PARA_DOM->nHdlr("same"))
        << "REQ: failed setLinkedHdlr() leaves no handler";
    EXPECT_EQ(0u, MSG_SELF->nMsg())
        << "REQ: failed setLinkedHdlr() leaves no callback";
}
TYPED_TEST_P(HdlrDominoTest, BugFix_invalidHdlr_noCrash)
{
    PARA_DOM->setHdlr("e1", nullptr);
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e1")) << "REQ: not create new Ev";

    PARA_DOM->setLinkedHdlr("alias e1", nullptr, "e1");
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("alias e1")) << "REQ: not create new Ev";

    PARA_DOM->setState({{"e1", true}});  // req: no crash

    EXPECT_CALL(*this, hdlr0());  // REQ: can add hdlr upon null
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr1());  // REQ: can add hdlr upon null
    PARA_DOM->setLinkedHdlr("alias e1", this->hdlr1_, "e1");
    this->pongMsgSelf_();
}

#define RM_HDLR
// ***********************************************************************************************
// rm hdlr, then F->T can't cb
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, rmHdlr_thenNoCallback)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    ASSERT_TRUE(PARA_DOM->rmOneHdlrOK("event"));

    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no call since rm
    PARA_DOM->setState({{"event", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, rmHdlr_fail)
{
    //EXPECT_FALSE(PARA_DOM->rmOneHdlrOK(Domino::D_EVENT_FAILED_RET, nullptr)) << "REQ: rm null hdlr";

    PARA_DOM->setHdlr("event", this->hdlr0_);
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event"));
    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("event")) << "REQ: rm unexist hdlr";

    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("not exist event")) << "REQ: rm unexist ev";

    //EXPECT_FALSE(PARA_DOM->rmOneHdlrOK(PARA_DOM->getEventBy("event"), nullptr)) << "REQ: rm null hdlr";
}
// ***********************************************************************************************
// rm on-road-hdlr
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, rmHdlrOnRoad_noCallback)
{
    PARA_DOM->setLinkedHdlr("e0", this->hdlr0_, "e");
    PARA_DOM->setLinkedHdlr("e1", this->hdlr1_, "e");
    PARA_DOM->setState({{"e", true}});  // cb on road
    EXPECT_TRUE(MSG_SELF->nMsg());
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e0")) << "REQ: rm hdlr on-road";

    EXPECT_CALL(*this, hdlr0()).Times(0);
    EXPECT_CALL(*this, hdlr1());
    this->pongMsgSelf_();  // manual trigger on road cb
}
TYPED_TEST_P(NofreeHdlrDominoTest, rmHdlrOnRoad_thenReAdd_noCallbackUntilReTrigger)
{
    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setState({{"event", true}});
    EXPECT_EQ(1U, MSG_SELF->nMsg(EMsgPri_NORM));  // 1 cb on road

    PARA_DOM->setState({{"event", false}});
    PARA_DOM->setState({{"event", true}});
    EXPECT_EQ(2U, MSG_SELF->nMsg(EMsgPri_NORM));  // 2 cb on road

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("event")) << "REQ: rm hdlr include all on-road";

    PARA_DOM->setState({{"event", false}});  // not auto-cb
    PARA_DOM->setHdlr("event", this->hdlr0_);  // re-add hdlr

    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no cb since rm-ed
    this->pongMsgSelf_();

    PARA_DOM->setState({{"event", true}});
    ASSERT_TRUE(MSG_SELF->nMsg());
    EXPECT_CALL(*this, hdlr0());  // REQ: new cb
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, hdlrOnRoad_thenRmDom_noCrash_noLeak)
{
    {
        auto dom = MAKE_PTR<TypeParam>(this->uniLogName());  // local; process dom stays
        dom->setHdlr("event", this->hdlr0_);
        dom->setState({{"event", true}});
        EXPECT_EQ(1U, MSG_SELF->nMsg(EMsgPri_NORM));  // 1 cb on road
    }  // REQ: rm dom
    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no cb
    this->pongMsgSelf_();
}

#define FORCE_CALL
// ***********************************************************************************************
// eg allow DatDom call hdlr directly after data changed, need not state F->T
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, GOLD_force_call)
{
    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no call
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();

    PARA_DOM->setHdlr("e1", this->hdlr0_);
    EXPECT_CALL(*this, hdlr0());  // REQ: force call
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, repeat_force_call)
{
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    EXPECT_CALL(*this, hdlr0());
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0());  // REQ: repeat force call
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();

    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: no call
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1")) << "REQ: rm hdlr";
    PARA_DOM->forceAllHdlr("e1");
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, replaceHdlr_newOneCalled)
{
    // REQ: eNB upgrade v1→v2: replace install handler, verify new one fires
    PARA_DOM->setHdlr("install", this->hdlr0_);
    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("install"));
    PARA_DOM->setHdlr("install", this->hdlr1_);  // different hdlr

    EXPECT_CALL(*this, hdlr0()).Times(0);  // REQ: old hdlr NOT called
    EXPECT_CALL(*this, hdlr1());            // REQ: new hdlr called
    PARA_DOM->setState({{"install", true}});
    this->pongMsgSelf_();
}
TYPED_TEST_P(NofreeHdlrDominoTest, lateRegister_immediateThenReTrigger)
{
    // REQ: monitor late-starts when condition already T → immediate call, then n-go retrigger
    PARA_DOM->setState({{"ready", true}});
    EXPECT_CALL(*this, hdlr0());
    PARA_DOM->setHdlr("ready", this->hdlr0_);  // immediate trigger
    this->pongMsgSelf_();

    // n-go cycle: F→T should retrigger the same hdlr
    PARA_DOM->setState({{"ready", false}});
    EXPECT_CALL(*this, hdlr0());
    PARA_DOM->setState({{"ready", true}});  // retrigger
    this->pongMsgSelf_();
}
TYPED_TEST_P(HdlrDominoTest, force_call_invalidEv)
{
    PARA_DOM->forceAllHdlr("invalid ev");
    EXPECT_EQ(0, MSG_SELF->nMsg()) << "inc code cov";
}

#define N_HDLR
// ***********************************************************************************************
// eg swm DownMgr use nHdlr to priority files
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, n_hdlr)
{
    EXPECT_EQ(0u, PARA_DOM->nHdlr("e1")) << "REQ: init no hdlr";

    PARA_DOM->setHdlr("e1", this->hdlr0_);
    EXPECT_EQ(1u, PARA_DOM->nHdlr("e1")) << "REQ: after added";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1")) << "REQ: rm hdlr";
    EXPECT_EQ(0u, PARA_DOM->nHdlr("e1")) << "REQ: after del";
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    PARA_DOM->setHdlr("e1", this->hdlr0_);
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e1"));
    EXPECT_EQ(1u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e1"));

    PARA_DOM->setLinkedHdlr("e2", this->hdlr2_, "e3");
    PARA_DOM->setLinkedHdlr("e2", this->hdlr2_, "e3");
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e2"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e3"));
    EXPECT_EQ(3u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e2"));
    EXPECT_FALSE(PARA_DOM->state("e3"));

    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);
    EXPECT_EQ(4u, this->uniqueEVs_.size());

    const auto e4 = PARA_DOM->getEventBy("e4");
    EXPECT_FALSE(PARA_DOM->rmOneHdlrOK("e4")) << "REQ: rm nonexist hdlr";
    EXPECT_EQ(e4, PARA_DOM->getEventBy("e4")) << "REQ: does not create";

    const auto e5 = PARA_DOM->getEventBy("e5");
    PARA_DOM->forceAllHdlr("e5");
    EXPECT_EQ(e5, PARA_DOM->getEventBy("e5")) << "REQ: does not create";

    const auto e6 = PARA_DOM->getEventBy("e6");
    PARA_DOM->nHdlr("e6");
    EXPECT_EQ(e6, PARA_DOM->getEventBy("e6")) << "REQ: does not create";
}

#define MSG_SELF_RELATED
// ***********************************************************************************************
TYPED_TEST_P(HdlrDominoTest, replace_msgSelf)  // checked by CI valgrind
{
    ASSERT_FALSE(PARA_DOM->setMsgSelfOK(nullptr)) << "REQ: msgSelf=null is not safe for HdlrDomino";

    PARA_DOM->setHdlr("event", this->hdlr0_);
    PARA_DOM->setState({{"event", true}});  // 1 msg in old msgSelf
    auto newMsgSelf = MAKE_PTR<MsgSelf>(this->uniLogName());
    ASSERT_FALSE(PARA_DOM->setMsgSelfOK(newMsgSelf)) << "REQ: can NOT set new msgSelf when unhandled msg in old";

    EXPECT_CALL(*this, hdlr0());  // drain old queue fires the pending hdlr
    this->pongMsgSelf_();
    ASSERT_TRUE(PARA_DOM->setMsgSelfOK(newMsgSelf)) << "REQ: can set new msgSelf";
    EXPECT_TRUE(PARA_DOM->setMsgSelfOK(MSG_SELF));  // restore process MsgSelf for soak test
}
TYPED_TEST_P(HdlrDominoTest, bugFix_invalidMsgSelf)  // checked by CI valgrind
{
    auto kept = MSG_SELF;
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK<MsgSelf>(nullptr, *this));  // rm MsgSelf from ObjAnywhere
    EXPECT_THROW(TypeParam(), std::runtime_error) << "REQ: TypeParam() shall throw when MsgSelf absent";

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(kept, *this));  // restore MsgSelf to ObjAnywhere
    EXPECT_FALSE(PARA_DOM->setMsgSelfOK(nullptr)) << "REQ: msgSelf=null is not allowed";
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(HdlrDominoTest
    , GOLD_add_and_call
    , immediate_call
    , except_hdlr

    , GOLD_trigger_chain_call
    , immediate_chain_call
    , wrongOrderPrev_wrongCallback
    , GOLD_safeUpgrade_allPrevBeforeState

    , multiHdlr_onOneEvent_nok
    , GOLD_multiHdlr_onDiffEvent_ok
    , multiHdlr_onOneAliasEvent_nok
    , setLinkedHdlr_selfLoop_failure_is_atomic
    , BugFix_invalidHdlr_noCrash

    , rmHdlr_thenNoCallback
    , rmHdlr_fail
    , rmHdlrOnRoad_noCallback

    , GOLD_force_call
    , force_call_invalidEv
    , n_hdlr

    , nonConstInterface_shall_createUnExistEvent_withStateFalse

    , replace_msgSelf
    , bugFix_invalidMsgSelf
);
using AnyHdlrDom = Types<MinHdlrDom, MinMhdlrDom, MinFreeDom, MinPriDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, HdlrDominoTest, AnyHdlrDom);

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(NofreeHdlrDominoTest
    , UC_reTrigger_reCall

    , GOLD_trigger_chain_many_calls

    , rmHdlrOnRoad_thenReAdd_noCallbackUntilReTrigger
    , hdlrOnRoad_thenRmDom_noCrash_noLeak

    , repeat_force_call
    , replaceHdlr_newOneCalled
    , lateRegister_immediateThenReTrigger
);
using AnyNofreeHdlrDom = Types<MinHdlrDom, MinMhdlrDom, MinPriDom, MaxNofreeDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, NofreeHdlrDominoTest, AnyNofreeHdlrDom);
}  // namespace
