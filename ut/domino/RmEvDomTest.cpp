/**
 * Copyright 2023 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gtest/gtest.h>
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::multiset;
using std::set;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct RmEvDomTest : public UtParaDom<aParaDom>
{
};

#define RM_DOM
// ***********************************************************************************************
template<class aParaDom> using RmDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmDomTest);

TYPED_TEST_P(RmDomTest, GOLD_rm_dom_resrc)
{
    EXPECT_FALSE(PARA_DOM->rmEvOK("invalid EN")) << "REQ: NOK to rm invalid Ev.";

    PARA_DOM->setPrev("e2", {{"e1", true}, {"e1b", true}});
    const auto e1 = PARA_DOM->setPrev("e1", {{"e0", false}});
    EXPECT_TRUE (PARA_DOM->state("e1"));
    EXPECT_FALSE(PARA_DOM->isRemoved(e1)) << "REQ: new ev is not removed state.";

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"))  << "REQ: OK to rm valid Ev.";
    EXPECT_TRUE(PARA_DOM->isRemoved(e1)) << "REQ: flag is set.";

    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: reset state of removed Ev.";

    PARA_DOM->setState({{"e0", true}});
    PARA_DOM->setState({{"e0", false}});
    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: e1's uplink is removed.";

    EXPECT_FALSE(PARA_DOM->state("e2"));
    PARA_DOM->setState({{"e1b", true}});
    EXPECT_TRUE(PARA_DOM->state("e2")) << "REQ: e1's downlink is removed.";

    EXPECT_TRUE(PARA_DOM->isRemoved(e1)) << "REQ: EN is removed.";
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e1")) << "REQ: EN is removed.";

    EXPECT_FALSE(PARA_DOM->rmEvOK("e1")) << "REQ: NOK to rm invalid Ev.";
}

TYPED_TEST_P(RmDomTest, GOLD_reuse_ev)
{
    PARA_DOM->setPrev("e2", {{"e1", true}});  // create e2 before e1 to inc cov of recycleEv_()
    const auto e1 = PARA_DOM->setPrev("e1", {{"e0", false}});

    EXPECT_TRUE (PARA_DOM->rmEvOK("e1"));
    EXPECT_EQ(e1, PARA_DOM->newEvent("new e1")) << "REQ: reuse removed ev as soon as possible.";
    EXPECT_FALSE(PARA_DOM->isRemoved(e1)) << "REQ: new ev is not removed state.";

    const auto e0 = PARA_DOM->getEventBy("e0");
    const auto e2 = PARA_DOM->getEventBy("e2");
    set<Domino::Event> evs = {e0, e2};

    EXPECT_TRUE(PARA_DOM->rmEvOK("e0")) << "REQ: can remove more ev.";
    EXPECT_TRUE(PARA_DOM->rmEvOK("e2")) << "REQ: existing multi removed ev.";
    EXPECT_EQ(1u, evs.count(PARA_DOM->newEvent("e3"))) << "REQ: can reuse removed ev.";
    EXPECT_EQ(1u, evs.count(PARA_DOM->newEvent("e4"))) << "REQ: can reuse removed ev.";
    EXPECT_NE(PARA_DOM->getEventBy("e3"), PARA_DOM->getEventBy("e4")) << "REQ: diff reused ev.";

    evs.insert(e1);
    EXPECT_EQ(0u, evs.count(PARA_DOM->newEvent("e100"))) << "REQ: recycle used-up, create new.";
}

TYPED_TEST_P(RmDomTest, bugFix_recycleShallNotGrowInternalStateSpace)
{
    const auto nSlot0 = PARA_DOM->nEvSlot();
    PARA_DOM->newEvent("reused ev");
    const auto nSlot1 = PARA_DOM->nEvSlot();
    EXPECT_LE(nSlot1, nSlot0 + 1u) << "REQ: one live ev uses at most one new slot";

    constexpr size_t nRound = 128;
    for (size_t i = 0; i < nRound; ++i)
    {
        EXPECT_TRUE(PARA_DOM->rmEvOK("reused ev")) << "REQ: can remove existing ev each round";
        PARA_DOM->newEvent("reused ev");
        EXPECT_EQ(PARA_DOM->nEvSlot(), nSlot1) << "REQ: recycle shall not inflate event space";
    }
}

TYPED_TEST_P(RmDomTest, doubleRemove_rejected)
{
    const auto e1 = PARA_DOM->newEvent("e1");
    EXPECT_FALSE(PARA_DOM->isRemoved(e1));

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"))    << "REQ: first remove shall succeed.";
    EXPECT_TRUE(PARA_DOM->isRemoved(e1))    << "REQ: flag is set after remove.";
    EXPECT_FALSE(PARA_DOM->rmEvOK("e1"))   << "REQ: double-remove shall be rejected.";
}

TYPED_TEST_P(RmDomTest, rmMiddle_thenRebuildLink_noFalseLoop)
{
    // REQ: rmEv completely clears all links — setPrev after rm shall not see ghost links
    // real scenario: version-ctrl domino rm old-version's middle event, then rewire
    //   A→B→C, rmEv(B), then setPrev("C", {{"A", T}}) → A→C (no loop)
    PARA_DOM->setPrev("B", {{"A", true}});
    PARA_DOM->setPrev("C", {{"B", true}});
    PARA_DOM->setState({{"A", true}});
    EXPECT_TRUE(PARA_DOM->state("C"));

    EXPECT_TRUE(PARA_DOM->rmEvOK("B"));
    // C's prev removed → vacuous AND = true (no constraints left)
    EXPECT_TRUE(PARA_DOM->state("C")) << "REQ: C has no prev after rm B → deduced T";

    // rewire: C directly depends on A (should not hit false loop detection from ghost B links)
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("C", {{"A", true}}))
        << "REQ: no false loop detected after rmEv";

    PARA_DOM->setState({{"A", false}});
    EXPECT_FALSE(PARA_DOM->state("C")) << "REQ: rewired chain reverse works";

    PARA_DOM->setState({{"A", true}});
    EXPECT_TRUE(PARA_DOM->state("C")) << "REQ: rewired chain forward works";
}

TYPED_TEST_P(RmDomTest, GOLD_nGo_fullLifecycle_createUseRmRepeat)
{
    // - REQ: eNB-upgrade-like lifecycle — build chain, run it, tear down, repeat with new events
    // - verifies: no ghost state/links survive across cycles, recycled IDs work clean
    // soak keeps the same DOM: recycle pool may already hold high IDs from earlier PARA tests
    const auto nSlot0 = PARA_DOM->nEvSlot();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        auto tag = "v" + std::to_string(cycle);
        auto head   = tag + "_start";
        auto middle = tag + "_check";
        auto tail   = tag + "_done";

        PARA_DOM->setPrev(middle, {{head, true}});
        PARA_DOM->setPrev(tail,   {{middle, true}});

        EXPECT_FALSE(PARA_DOM->state(tail));
        PARA_DOM->setState({{head, true}});
        EXPECT_TRUE(PARA_DOM->state(tail)) << "REQ: cycle " << cycle << " chain works";

        // tear down entire chain
        EXPECT_TRUE(PARA_DOM->rmEvOK(tail));
        EXPECT_TRUE(PARA_DOM->rmEvOK(middle));
        EXPECT_TRUE(PARA_DOM->rmEvOK(head));
    }
    // 3-tile chain needs at most 3 new slots; later cycles + probe must recycle
    EXPECT_LE(PARA_DOM->nEvSlot(), nSlot0 + 3u) << "REQ: later cycles recycle, no unbounded growth";
    const auto nSlot1 = PARA_DOM->nEvSlot();
    auto probe = PARA_DOM->newEvent("probe");
    EXPECT_EQ(PARA_DOM->nEvSlot(), nSlot1) << "REQ: IDs recycled across cycles, no unbounded growth";
    EXPECT_LT(probe, nSlot1) << "REQ: probe reuses a recycled slot";
}

REGISTER_TYPED_TEST_SUITE_P(RmDomTest
    , GOLD_rm_dom_resrc
    , GOLD_reuse_ev
    , bugFix_recycleShallNotGrowInternalStateSpace
    , doubleRemove_rejected
    , rmMiddle_thenRebuildLink_noFalseLoop
    , GOLD_nGo_fullLifecycle_createUseRmRepeat
);
using AnyRmDom = Types<MinRmEvDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmDomTest, AnyRmDom);

#define RM_DATA_DOM
// ***********************************************************************************************
template<class aParaDom> using RmDataDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmDataDomTest);

TYPED_TEST_P(RmDataDomTest, GOLD_rm_DataDom_resrc)
{
    struct TestData
    {
        bool& isDestructed_;
        explicit TestData(bool& aExtFlag) : isDestructed_(aExtFlag) { isDestructed_ = false; }
        ~TestData() { isDestructed_ = true; }
    };
    bool isDestructed;

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev", MAKE_PTR<TestData>(isDestructed)))
        << "REQ: set data ok";
    EXPECT_FALSE(isDestructed);
    const auto ev = PARA_DOM->getEventBy("ev");

    EXPECT_TRUE(PARA_DOM->rmEvOK("ev")) << "REQ: rm succ.";
    EXPECT_TRUE(isDestructed) << "REQ: data is removed.";
    EXPECT_EQ(nullptr, PARA_DOM->getData("ev").get()) << "REQ: get null after removed.";

    EXPECT_EQ(ev, PARA_DOM->newEvent("another ev"))  << "REQ: reuse ev.";
    EXPECT_EQ(nullptr, PARA_DOM->getData("another ev").get()) << "REQ: reuse ev's data space.";
}

REGISTER_TYPED_TEST_SUITE_P(RmDataDomTest
    , GOLD_rm_DataDom_resrc
);
using AnyRmDataDom = Types<MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmDataDomTest, AnyRmDataDom);

#define RM_W_DATA_DOM
// ***********************************************************************************************
template<class aParaDom> using RmWdatDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmWdatDomTest);

TYPED_TEST_P(RmWdatDomTest, GOLD_rm_WdatDom_resrc)
{
    struct TestData
    {
        bool& isDestructed_;
        explicit TestData(bool& aExtFlag) : isDestructed_(aExtFlag) { isDestructed_ = false; }
        ~TestData() { isDestructed_ = true; }
    };
    bool isDestructed;

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev", true)) << "REQ: test wctrl data.";
    EXPECT_TRUE(PARA_DOM->wbasic_replaceDataOK("ev", MAKE_PTR<TestData>(isDestructed)))
        << "REQ: set wr-data ok";
    EXPECT_FALSE(isDestructed);
    const auto ev = PARA_DOM->getEventBy("ev");

    EXPECT_TRUE(PARA_DOM->rmEvOK("ev")) << "REQ: rm succ.";
    EXPECT_TRUE(isDestructed) << "REQ: data is removed.";
    EXPECT_EQ(nullptr, PARA_DOM->wbasic_getData("ev").get()) << "REQ: get null after removed.";
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev")) << "REQ: reset wctrl flag.";

    EXPECT_FALSE(PARA_DOM->rmEvOK("ev")) << "REQ: fail to rm invalid.";

    EXPECT_EQ(ev, PARA_DOM->newEvent("another ev"))  << "REQ: reuse ev.";
    EXPECT_EQ(nullptr, PARA_DOM->wbasic_getData("another ev").get()) << "REQ: reuse ev's data space.";
}

REGISTER_TYPED_TEST_SUITE_P(RmWdatDomTest
    , GOLD_rm_WdatDom_resrc
);
using AnyRmWdatDom = Types<MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmWdatDomTest, AnyRmWdatDom);

#define RM_HDLR_DOM
// ***********************************************************************************************
template<class aParaDom> using RmHdlrDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmHdlrDomTest);

TYPED_TEST_P(RmHdlrDomTest, GOLD_rm_HdlrDom_resrc)
{
    multiset<int> hdlrIDs;
    auto e1 = PARA_DOM->setHdlr("e1", [&hdlrIDs](){ hdlrIDs.insert(1); });
    PARA_DOM->setLinkedHdlr("e2", [&hdlrIDs](){ hdlrIDs.insert(2); }, "e1");
    PARA_DOM->setState({{"e1", true}});
    EXPECT_EQ(2u, MSG_SELF->nMsg()) << "REQ: 2 hdlrs on road.";
    EXPECT_EQ(0u, hdlrIDs.size()) << "REQ: not callback yet.";

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"));
    EXPECT_EQ(e1, PARA_DOM->setHdlr("another e1", [&hdlrIDs](){ hdlrIDs.insert(3); }))  << "REQ: reuse e1.";
    PARA_DOM->forceAllHdlr("another e1");
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e2")) << "REQ: rm ev not impact its alias.";
    EXPECT_EQ(3u, MSG_SELF->nMsg()) << "REQ: another e1's hdlr is on road.";

    EXPECT_TRUE(PARA_DOM->rmEvOK("e2")) << "REQ: can rm alias Ev.";
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>{3}, hdlrIDs) << "REQ: no 2 removed evs' hdlrs";
}

TYPED_TEST_P(RmHdlrDomTest, rmTruePrev_callHdlr_ifSatisfied)
{
    multiset<int> hdlrIDs;
    PARA_DOM->setHdlr("e0", [&hdlrIDs](){ hdlrIDs.insert(0); });
    PARA_DOM->setPrev("e0", {{"e1", true}, {"e2", false}});
    EXPECT_FALSE(PARA_DOM->state("e0"));

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1")) << "req: rm succ";
    EXPECT_TRUE(PARA_DOM->state("e0")) << "REQ: deduce related next";
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>{0}, hdlrIDs) << "REQ: prerequisite satisfied -> call hdlr.";
}
TYPED_TEST_P(RmHdlrDomTest, rmFalsePrev_callHdlr_ifSatisfied)
{
    multiset<int> hdlrIDs;
    PARA_DOM->setHdlr("e0", [&hdlrIDs](){ hdlrIDs.insert(0); });
    PARA_DOM->setPrev("e0", {{"e1", true}, {"e2", false}});
    PARA_DOM->setState({{"e1", true}, {"e2", true}});
    EXPECT_FALSE(PARA_DOM->state("e0"));

    EXPECT_TRUE(PARA_DOM->rmEvOK("e2")) << "req: rm succ";
    EXPECT_TRUE(PARA_DOM->state("e0")) << "REQ: deduce related next";
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>{0}, hdlrIDs) << "REQ: prerequisite satisfied -> call hdlr.";
}

REGISTER_TYPED_TEST_SUITE_P(RmHdlrDomTest
    , GOLD_rm_HdlrDom_resrc
    , rmFalsePrev_callHdlr_ifSatisfied
    , rmTruePrev_callHdlr_ifSatisfied
);
using AnyRmHdlrDom = Types<MaxNofreeDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmHdlrDomTest, AnyRmHdlrDom);

#define RM_FREE_HDLR_DOM
// ***********************************************************************************************
template<class aParaDom> using RmFreeHdlrDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmFreeHdlrDomTest);

TYPED_TEST_P(RmFreeHdlrDomTest, GOLD_rm_FreeHdlrDom_resrc)
{
    auto e1 = PARA_DOM->repeatedHdlr("e1");
    EXPECT_TRUE(PARA_DOM->isRepeatHdlr(e1));

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"));
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: rm Ev shall clear auto-free flag.";

    EXPECT_EQ(e1, PARA_DOM->repeatedHdlr("another e1")) << "REQ: reuse e1.";
    EXPECT_TRUE(PARA_DOM->isRepeatHdlr(e1));
}

REGISTER_TYPED_TEST_SUITE_P(RmFreeHdlrDomTest
    , GOLD_rm_FreeHdlrDom_resrc
);
using AnyRmFreeHdlrDom = Types<MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmFreeHdlrDomTest, AnyRmFreeHdlrDom);

#define RM_PRI_DOM
// ***********************************************************************************************
template<class aParaDom> using RmPriDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmPriDomTest);

TYPED_TEST_P(RmPriDomTest, GOLD_rm_PriDom_resrc)
{
    auto e1 = PARA_DOM->setPriority("e1", EMsgPriority::EMsgPri_LOW);
    EXPECT_EQ(EMsgPriority::EMsgPri_LOW, PARA_DOM->getPriority(e1));

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"));
    EXPECT_EQ(EMsgPriority::EMsgPri_NORM, PARA_DOM->getPriority(e1)) << "REQ: reset pri.";

    EXPECT_EQ(e1, PARA_DOM->setPriority("e1", EMsgPriority::EMsgPri_HIGH)) << "REQ: reuse e1.";
    EXPECT_EQ(EMsgPriority::EMsgPri_HIGH, PARA_DOM->getPriority(e1)) << "REQ: new pri";
}

REGISTER_TYPED_TEST_SUITE_P(RmPriDomTest
    , GOLD_rm_PriDom_resrc
);
using AnyRmPriDom = Types<MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmPriDomTest, AnyRmPriDom);

#define RM_M_HDLR_DOM
// ***********************************************************************************************
template<class aParaDom> using RmMhdlrDomTest = RmEvDomTest<aParaDom>;
TYPED_TEST_SUITE_P(RmMhdlrDomTest);

TYPED_TEST_P(RmMhdlrDomTest, GOLD_rm_MhdlrDom_resrc)
{
    multiset<int> hdlrIDs;
    auto e1 = PARA_DOM->setHdlr("e1", [&hdlrIDs](){ hdlrIDs.insert(1); });
    PARA_DOM->multiHdlrOnSameEv("e1", [&hdlrIDs](){ hdlrIDs.insert(2); }, "h2");

    PARA_DOM->setState({{"e1", true}});
    EXPECT_EQ(2u, MSG_SELF->nMsg()) << "REQ: 2 hdlrs on road.";
    EXPECT_EQ(0u, hdlrIDs.size()) << "REQ: not callback yet.";

    EXPECT_TRUE(PARA_DOM->rmEvOK("e1"));
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>{}, hdlrIDs) << "REQ: not exe e2 hdlr since removed.";

    EXPECT_EQ(e1, PARA_DOM->multiHdlrOnSameEv("reuse e1", [&hdlrIDs](){ hdlrIDs.insert(3); }, "h3"));
    PARA_DOM->forceAllHdlr("reuse e1");
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>{3}, hdlrIDs) << "REQ: exe new hdlr.";
}

REGISTER_TYPED_TEST_SUITE_P(RmMhdlrDomTest
    , GOLD_rm_MhdlrDom_resrc
);
using AnyRmMhdlrDom = Types<MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, RmMhdlrDomTest, AnyRmMhdlrDom);

}  // namespace
