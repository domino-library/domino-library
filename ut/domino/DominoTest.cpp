/**
 * Copyright 2016 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <chrono>
#include <memory>  // for shared_ptr
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::set;
using std::string;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct DominoTest : public UtParaDom<aParaDom>
{
    set<Domino::Event> uniqueEVs_;
};
TYPED_TEST_SUITE_P(DominoTest);

#define STATE
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, GOLD_setState_thenGetIt)
{
    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: non-exist ev's state=F";

    PARA_DOM->newEvent("e1");
    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: new ev's state=F";

    EXPECT_EQ(1u, PARA_DOM->setState({{"e1", true}})) << "REQ: set T succ";
    EXPECT_TRUE(PARA_DOM->state("e1")) << "REQ: set T then get it";
    EXPECT_EQ(0u, PARA_DOM->setState({{"e1", true}})) << "REQ: no state change";
    EXPECT_TRUE(PARA_DOM->state("e1")) << "REQ: no state change";

    EXPECT_EQ(1u, PARA_DOM->setState({{"e1", false}})) << "REQ: set F succ";
    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: set F then get it";
    EXPECT_EQ(0u, PARA_DOM->setState({{"e1", false}})) << "REQ: no state change";
    EXPECT_FALSE(PARA_DOM->state("e1")) << "REQ: no state change";
}
TYPED_TEST_P(DominoTest, invalidEv_retStateFalse)
{
    EXPECT_FALSE(PARA_DOM->state(Domino::D_EVENT_FAILED_RET)) << "REQ: invalid event returns false";
    EXPECT_FALSE(PARA_DOM->state(99999)) << "REQ: out-of-range event returns false";
}

#define BROADCAST_STATE
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, GOLD_forward_broadcast_trueLink)
{
    // e1->e2->e3
    PARA_DOM->setPrev("e2", {{"e1", true}});
    PARA_DOM->setPrev("e3", {{"e2", true}});
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_FALSE(PARA_DOM->state("e2"));
    EXPECT_FALSE(PARA_DOM->state("e3"));

    PARA_DOM->setState({{"e1", true}});  // set beginning
    EXPECT_TRUE(PARA_DOM->state("e1"));
    EXPECT_TRUE(PARA_DOM->state("e2")) << "REQ: broadcast T e1->e2";
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: broadcast T e2->e3";

    PARA_DOM->setState({{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_FALSE(PARA_DOM->state("e2")) << "REQ: broadcast F e1->e2";
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: broadcast F e2->e3";
}
TYPED_TEST_P(DominoTest, GOLD_forward_broadcast_falseLink)
{
    // e1->e2->e3
    PARA_DOM->setPrev("e3", {{"e2", false}});
    PARA_DOM->setPrev("e2", {{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_TRUE (PARA_DOM->state("e2")) << "REQ: broadcast F e1->e2";
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: broadcast T e2->e3";

    PARA_DOM->setState({{"e1", true}});  // set beginning
    EXPECT_TRUE (PARA_DOM->state("e1"));
    EXPECT_FALSE(PARA_DOM->state("e2")) << "REQ: broadcast T e1->e2";
    EXPECT_TRUE (PARA_DOM->state("e3")) << "REQ: broadcast F e2->e3";
}
TYPED_TEST_P(DominoTest, setState_onlyAtChainHead)
{
    // e1->e2->e3
    PARA_DOM->setPrev("e3", {{"e2", true}});
    PARA_DOM->setPrev("e2", {{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_TRUE (PARA_DOM->state("e2"));
    EXPECT_TRUE (PARA_DOM->state("e3"));

    EXPECT_EQ(0u, PARA_DOM->setState({{"e2", true}})) << "REQ: fail set middle";
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_TRUE (PARA_DOM->state("e2")) << "REQ: fail set middle";
    EXPECT_TRUE (PARA_DOM->state("e3"));

    EXPECT_EQ(0u, PARA_DOM->setState({{"e3", true}})) << "REQ: fail set end";
    EXPECT_FALSE(PARA_DOM->state("e1"));
    EXPECT_TRUE (PARA_DOM->state("e2"));
    EXPECT_TRUE (PARA_DOM->state("e3")) << "REQ: fail set end";

    // REQ: atomic reject — batch mixing valid head + invalid non-head rejects ALL
    PARA_DOM->newEvent("h1");
    EXPECT_EQ(0u, PARA_DOM->setState({{"h1", true}, {"e2", true}})) << "REQ: 1 non-head -> reject ALL";
    EXPECT_FALSE(PARA_DOM->state("h1")) << "REQ: valid head also not set";
}
TYPED_TEST_P(DominoTest, bugFix_shallDeduceAll)
{
    PARA_DOM->setPrev("e2", {{"e1", true}});
    PARA_DOM->setState({
        {"e0", false},  // no-next e0 shall not abort deducing e1
        {"e1", true},
        {"e0", false}});
    EXPECT_TRUE(PARA_DOM->state("e2"));
}
TYPED_TEST_P(DominoTest, GOLD_simuSetState)
{
    PARA_DOM->setPrev("e2", {{"e1", true}});
    PARA_DOM->setPrev("e4", {{"e3", true}});

    EXPECT_EQ(2u, PARA_DOM->setState({{"e1", true}, {"e3", true}}));
    EXPECT_TRUE(PARA_DOM->state("e2"));
    EXPECT_TRUE(PARA_DOM->state("e4"));
}
TYPED_TEST_P(DominoTest, GOLD_nGo_repeatBroadcastCycle)
{
    // REQ: n-go domino — same chain can cycle T→F→T repeatedly (like IM repeat working till reboot)
    // e1->e2->e3
    PARA_DOM->setPrev("e2", {{"e1", true}});
    PARA_DOM->setPrev("e3", {{"e2", true}});

    // cycle 1
    PARA_DOM->setState({{"e1", true}});
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: cycle1 broadcast T";
    PARA_DOM->setState({{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: cycle1 broadcast F";

    // cycle 2 — exact same chain re-triggers
    PARA_DOM->setState({{"e1", true}});
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: cycle2 broadcast T (n-go)";
    PARA_DOM->setState({{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: cycle2 broadcast F (n-go)";
}

#define PREV
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, GOLD_multi_allPrevSatisfied_thenPropagate)
{
    // e3 depends on e1(T) AND e2(F)
    PARA_DOM->setPrev("e3", {{"e1", true}, {"e2", false}});
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: not all prev satisfied yet";

    PARA_DOM->setState({{"e1", true}});
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: all prev satisfied => propagate T";

    PARA_DOM->setState({{"e2", true}});  // now e2==true breaks link=false
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: one prev unsatisfied => propagate F";

    PARA_DOM->setState({{"e1", false}});  // e1==false breaks link=true too
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: no prev satisfied => stay F";
    // REQ: cumulative setPrev - add more prev to e3
    PARA_DOM->setPrev("e3", {{"e4", true}});  // add second prev e4(T)
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: both old & new prev unsatisfied";
    PARA_DOM->setState({{"e1", true}});  // restore e1=T
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: still need e2 & e4 both T";
    PARA_DOM->setState({{"e2", false}, {"e4", true}});  // restore e2=F, set e4=T
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: all prev satisfied => propagate T";
}
TYPED_TEST_P(DominoTest, invalid_loopSelf)
{
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e1", {{"e1", true }})) << "REQ: can't loop self";
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e1", {{"e1", false}})) << "REQ: can't loop self";
}
TYPED_TEST_P(DominoTest, invalid_deepLoop)
{
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e0", {{"e1", true}}));
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e1", {{"e0", true}})) << "REQ: can't deep loop";
}
TYPED_TEST_P(DominoTest, invalid_deeperLoop)
{
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e0", {{"e1", false}}));
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e1", {{"e2", false}}));
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e2", {{"e0", false}})) << "REQ: can't deeper loop";
}
TYPED_TEST_P(DominoTest, invalid_mixLoop)
{
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e0", {{"e1", false}}));
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e1", {{"e0", true}})) << "REQ: can't T/F mix loop";
}
TYPED_TEST_P(DominoTest, whyFalse_diagnoseTrueFalseConflict)
{
    // e11 <- (T) <- e10
    //    \         /
    //     <- (F) <-
    auto e10 = PARA_DOM->setPrev("e10", {{"e11", true }});
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("e10", {{"e11", false}}))
        << "REQ: reject direct T/F conflict on same source";
    EXPECT_EQ("e11==false", PARA_DOM->whyFalse(e10)) << "REQ: only true-prev exists";

    // indirect T/F conflicts (different sources per call) are still allowed:
    // e21 <- (F) <- e20
    // (T)\         /(T)
    //     <- e22 <-
    auto e20 = PARA_DOM->setPrev("e20", {{"e21", false}, {"e22", true}});
    PARA_DOM->setPrev("e22", {{"e21", true}});
    EXPECT_EQ("e21==false", PARA_DOM->whyFalse(e20)) << "REQ: simply found the futhest root cause";
    PARA_DOM->setState({{"e21", true}});
    EXPECT_EQ("e21==true",  PARA_DOM->whyFalse(e20)) << "REQ: simply found the futhest root cause";

    // e31 <--------------- (F) <- e30
    // (F)\                       /(F)
    //     <- e33 <- (T) <- e32 <-
    PARA_DOM->setPrev("e33", {{"e31", false}});
    PARA_DOM->setPrev("e32", {{"e33", true}});
    auto e30 = PARA_DOM->setPrev("e30", {{"e31", false}, {"e32", false}});
    EXPECT_EQ("e31==false", PARA_DOM->whyFalse(e30)) << "REQ: simply found the futhest root cause";
    PARA_DOM->setState({{"e31", true}});
    EXPECT_EQ("e31==true",  PARA_DOM->whyFalse(e30)) << "REQ: simply found the futhest root cause";

    // e44 <-(F)- e43 <-(T)- e41 <-(T)- e40
    //               \                 /
    //                <-(T)- e42 <-(F)-
    auto e40 = PARA_DOM->setPrev("e40", {{"e41", true}, {"e42", false}});
    PARA_DOM->setPrev("e41", {{"e43", true}});
    PARA_DOM->setPrev("e42", {{"e43", true}});
    EXPECT_EQ("e43==false", PARA_DOM->whyFalse(e40));
    PARA_DOM->setPrev("e43", {{"e44", false}});
    EXPECT_EQ("e44==false", PARA_DOM->whyFalse(e40)) << "inc cov: e40=T via e41, then =F via e42";

    // - indirect T/F conflict can be very long & complex (much more than above examples)
    //   . when occur, the end-event can't be satisfied forever
    // - direct T/F conflict (same source as both T & F prev for same target) is now prevented
    //   . whyFalse() can detect indirect conflicts (but can't prevent)
    // - how to define loop-safe:
    //   . runtime forbid (rather than offline/afterward check which is not safe)
    //   . so shall fail setPrev() to prevent loop
}
TYPED_TEST_P(DominoTest, GOLD_diamond_broadcast)
{
    //   e1  e2
    //  / \ /
    // e3 e4
    //  \ /
    //   e5
    PARA_DOM->setPrev("e3", {{"e1", true}});
    PARA_DOM->setPrev("e4", {{"e1", true}, {"e2", true}});
    PARA_DOM->setPrev("e5", {{"e3", true}, {"e4", true}});

    PARA_DOM->setState({{"e1", true}, {"e2", true}});
    EXPECT_TRUE(PARA_DOM->state("e5")) << "REQ: e5=T (may re-deduce via e2)";

    PARA_DOM->setState({{"e2", false}});
    EXPECT_FALSE(PARA_DOM->state("e4"));
    EXPECT_FALSE(PARA_DOM->state("e5"));

    PARA_DOM->setState({{"e2", true}});
    EXPECT_TRUE(PARA_DOM->state("e4"));
    EXPECT_TRUE(PARA_DOM->state("e5"));
}
TYPED_TEST_P(DominoTest, setPrev_lateConnect_deducesImmediately)
{
    // REQ(auto shape): connect dependency AFTER predecessor already satisfied → immediate deduction
    // real scenario: agent1 done first, then monitor registers dependency later
    PARA_DOM->setState({{"agent1_done", true}});
    PARA_DOM->setPrev("monitor_ok", {{"agent1_done", true}});
    EXPECT_TRUE(PARA_DOM->state("monitor_ok")) << "REQ: late connect, prev already T → target T immediately";

    // REQ: the reverse — setPrev overrides a manually-set state when prev not satisfied
    // real scenario: manually set flag=T, then later bind it to dependency chain
    PARA_DOM->setState({{"flag", true}});
    EXPECT_TRUE(PARA_DOM->state("flag"));
    PARA_DOM->setPrev("flag", {{"gate", true}});  // gate=F → flag deduced to F, overriding manual T
    EXPECT_FALSE(PARA_DOM->state("flag")) << "REQ: setPrev overrides manual state via deduction";

    // REQ: after setPrev, flag is non-head → setState must be rejected
    EXPECT_EQ(0u, PARA_DOM->setState({{"flag", true}})) << "REQ: head→non-head, setState rejected";
}
TYPED_TEST_P(DominoTest, buildOrder_bottomUp_sameResult)
{
    // REQ(auto shape): different teams build the same graph in different order → same behavior
    // real scenario: team C registers "I depend on B" before team B registers "I depend on A"
    // build bottom-up: e3←e2 first, then e2←e1
    PARA_DOM->setPrev("e3", {{"e2", true}});
    PARA_DOM->setPrev("e2", {{"e1", true}});

    PARA_DOM->setState({{"e1", true}});
    EXPECT_TRUE(PARA_DOM->state("e2"));
    EXPECT_TRUE(PARA_DOM->state("e3")) << "REQ: bottom-up build same result as top-down";

    PARA_DOM->setState({{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e3")) << "REQ: bottom-up build reverse also works";
}
TYPED_TEST_P(DominoTest, asymmetricConverge_singleHead)
{
    // REQ: single head fans out to paths of different depth, then reconverge
    // real scenario: "start" triggers fast-check(1 hop) and slow-download-install(2 hops),
    //   merge_point needs both done
    //       start
    //      /     \                       (both true-link)
    //   fast    slow
    //     |       |                      (both true-link)
    //     |     install
    //      \     /                       (both true-link)
    //       merge
    PARA_DOM->setPrev("fast",    {{"start", true}});
    PARA_DOM->setPrev("slow",    {{"start", true}});
    PARA_DOM->setPrev("install", {{"slow",  true}});
    PARA_DOM->setPrev("merge",   {{"fast",  true}, {"install", true}});

    PARA_DOM->setState({{"start", true}});
    EXPECT_TRUE(PARA_DOM->state("merge")) << "REQ: asymmetric paths both done => merge T";

    PARA_DOM->setState({{"start", false}});
    EXPECT_FALSE(PARA_DOM->state("merge")) << "REQ: asymmetric paths both reverted => merge F";
}
TYPED_TEST_P(DominoTest, dupSetPrev_isIdempotent)
{
    // REQ: repeated setPrev with same args must not create dup links (no wrong multi-trigger)
    PARA_DOM->setPrev("e2", {{"e1", true}});
    PARA_DOM->setPrev("e2", {{"e1", true}});  // dup call

    PARA_DOM->setState({{"e1", true}});
    EXPECT_TRUE(PARA_DOM->state("e2"));

    PARA_DOM->setState({{"e1", false}});
    EXPECT_FALSE(PARA_DOM->state("e2")) << "REQ: no dup link means clean F propagation";
}
TYPED_TEST_P(DominoTest, setPrev_failedNoLink)
{
    // REQ: when setPrev fails (loop/conflict), no link is created — even for preceding valid prevs
    // real scenario: user accidentally includes a loop in batch setPrev, expects clean rollback
    //
    // pre-existing chain: step1 -> step2
    PARA_DOM->setPrev("step2", {{"step1", true}});
    PARA_DOM->setState({{"step1", true}});
    EXPECT_TRUE(PARA_DOM->state("step2"));

    // attempt: setPrev("step1", {{"step3", T}, {"step2", T}}) — step2 is next of step1 → loop!
    // step3(valid) comes before step2(invalid) in map iteration — tests atomic rejection
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->setPrev("step1", {{"step2", true}, {"step3", true}}));

    // REQ: step3 should NOT be linked as prev of step1 despite being valid itself
    // verify: step1 is still a chain head (no prev), setState still works on it
    EXPECT_EQ(1u, PARA_DOM->setState({{"step1", false}})) << "REQ: step1 still a head (no prev created)";
    EXPECT_FALSE(PARA_DOM->state("step2"));
    EXPECT_EQ(1u, PARA_DOM->setState({{"step1", true}}));
    EXPECT_TRUE(PARA_DOM->state("step2")) << "REQ: original chain intact";
}

#define WHY_FALSE
// ***********************************************************************************************
// req: SmodAgent's file-need-check is based on 200+ files, too hard to debug why decide not to rom/plan
// - ask Domino to find 1 prev-event
//   . so SmodAgent can log_ << PARA_DOM.whyFalse(EnSmod_IS_FNC_TO_ROM_PLAN)
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, GOLD_multi_retOne)
{
    auto master = PARA_DOM->setPrev("master succ", {{"all agents succ", true}, {"user abort", false}});
    EXPECT_EQ("all agents succ==false", PARA_DOM->whyFalse(master)) << "REQ: get 1 root cause";

    PARA_DOM->setState({{"all agents succ", true}, {"user abort", true}});  // REQ: simultaneous state
    EXPECT_EQ("user abort==true", PARA_DOM->whyFalse(master)) << "REQ: get diff root cause";
}
TYPED_TEST_P(DominoTest, trueEvent_retEmpty)
{
    auto master = PARA_DOM->setPrev("master succ", {{"all agents succ", true}, {"user abort", false}});
    PARA_DOM->setState({{"all agents succ", true}});
    EXPECT_TRUE(PARA_DOM->state("master succ"));
    EXPECT_EQ("[Dom Reserved EvName] whyFalse() found nothing", PARA_DOM->whyFalse(master));
}
TYPED_TEST_P(DominoTest, eventWithoutPrev_retSelf)
{
    auto no_prev_ev = PARA_DOM->newEvent("no prev ev");
    EXPECT_FALSE(PARA_DOM->state("no prev ev"));
    EXPECT_EQ("no prev ev==false", PARA_DOM->whyFalse(no_prev_ev));
}
TYPED_TEST_P(DominoTest, invalidEvent_retEmpty)
{
    EXPECT_EQ("[Dom Reserved EvName] whyFalse() found nothing", PARA_DOM->whyFalse(Domino::D_EVENT_FAILED_RET));
    EXPECT_EQ("[Dom Reserved EvName] whyFalse() found nothing", PARA_DOM->whyFalse(PARA_DOM->nEvSlot()));
}
TYPED_TEST_P(DominoTest, incCov_whyFalse_whyTrue)
{
    // e12
    // (T)\_
    //     <- e11 <- (F) <- e10
    // (F)/
    // e13
    PARA_DOM->setPrev("e11", {{"e12", true}, {"e13", false}});
    PARA_DOM->setState({{"e12", true}});
    auto e10 = PARA_DOM->setPrev("e10", {{"e11", false}});
    EXPECT_EQ("e11==true", PARA_DOM->whyFalse(e10)) << "REQ: inc branch coverage";
}

#define SEARCH_EVNAME
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, search_partial_evName)
{
    PARA_DOM->newEvent("/A");
    PARA_DOM->newEvent("/A/B");
    auto&& evNames = PARA_DOM->evNames();

    bool foundA = false, foundAB = false;
    size_t nX = 0;
    for (auto&& evName : evNames)
    {
        if (evName == "/A") foundA = true;
        if (evName == "/A/B") foundAB = true;
        if (evName.find("/X") != string::npos) ++nX;
    }
    EXPECT_TRUE(foundA) << "REQ: found";
    EXPECT_TRUE(foundAB) << "REQ: found";
    EXPECT_EQ(0u, nX) << "REQ: not found";
}
TYPED_TEST_P(DominoTest, search_all_evNames)
{
    // REQ: evNames() must contain all created events - completeness verification
    PARA_DOM->newEvent("e1");
    PARA_DOM->newEvent("e2");
    PARA_DOM->newEvent("e3");

    auto&& evNames = PARA_DOM->evNames();
    EXPECT_GE(evNames.size(), 3u) << "REQ: evNames should contain all 3 created events";

    // Verify each created event is in the returned container
    bool found_e1 = false, found_e2 = false, found_e3 = false;
    for (auto&& evName : evNames) {
        if (evName == "e1") found_e1 = true;
        if (evName == "e2") found_e2 = true;
        if (evName == "e3") found_e3 = true;
    }
    EXPECT_TRUE(found_e1) << "REQ: e1 must be in evNames()";
    EXPECT_TRUE(found_e2) << "REQ: e2 must be in evNames()";
    EXPECT_TRUE(found_e3) << "REQ: e3 must be in evNames()";
}

#define ID
// ***********************************************************************************************
// req: both event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(DominoTest, getEventBy_existing_event)
{
    auto ev = PARA_DOM->newEvent("e1");
    EXPECT_EQ(ev, PARA_DOM->getEventBy("e1")) << "REQ: get existing event";
}
TYPED_TEST_P(DominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    // req: new ID by newEvent()
    this->uniqueEVs_.insert(PARA_DOM->newEvent(""));  // REQ: create new
    this->uniqueEVs_.insert(PARA_DOM->newEvent(""));  // REQ: no dup
    EXPECT_EQ(1u, this->uniqueEVs_.size());

    // req: new ID by setState()
    PARA_DOM->setState({{"", false}, {"e2", false}});
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e2"));
    PARA_DOM->setState({{"", false}, {"e2", false}});
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e2"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());

    // req: new ID by setPrev()
    PARA_DOM->setPrev("e3", {{"e4", true}});
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e3"));
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e4"));
    PARA_DOM->setPrev("e3", {{"e4", true}});
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e3"));
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e4"));
    EXPECT_EQ(4u, this->uniqueEVs_.size());

    // REQ: newEvent() with explicit name is idempotent (Event:EvName=1:1)
    auto ev1 = PARA_DOM->newEvent("myEvent");
    auto ev2 = PARA_DOM->newEvent("myEvent");
    EXPECT_EQ(ev1, ev2) << "REQ: repeated newEvent() with same name returns same Event";

    this->uniqueEVs_.insert(ev1);  // add myEvent to set
    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);  // REQ: new ID != Domino::D_EVENT_FAILED_RET
    EXPECT_EQ(6u, this->uniqueEVs_.size());
}
TYPED_TEST_P(DominoTest, noID_for_not_exist_EvName)
{
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("no-such-ev"));
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(DominoTest
    , GOLD_setState_thenGetIt
    , invalidEv_retStateFalse

    , GOLD_forward_broadcast_trueLink
    , GOLD_forward_broadcast_falseLink
    , setState_onlyAtChainHead
    , bugFix_shallDeduceAll
    , GOLD_simuSetState
    , GOLD_nGo_repeatBroadcastCycle

    , GOLD_multi_allPrevSatisfied_thenPropagate
    , invalid_loopSelf
    , invalid_deepLoop
    , invalid_deeperLoop
    , invalid_mixLoop
    , whyFalse_diagnoseTrueFalseConflict
    , GOLD_diamond_broadcast
    , setPrev_lateConnect_deducesImmediately
    , buildOrder_bottomUp_sameResult
    , asymmetricConverge_singleHead
    , dupSetPrev_isIdempotent
    , setPrev_failedNoLink

    , GOLD_multi_retOne
    , trueEvent_retEmpty
    , eventWithoutPrev_retSelf
    , invalidEvent_retEmpty
    , incCov_whyFalse_whyTrue

    , search_partial_evName
    , search_all_evNames

    , getEventBy_existing_event
    , nonConstInterface_shall_createUnExistEvent_withStateFalse
    , noID_for_not_exist_EvName
);
using AnyDom = Types<Domino, MinDatDom, MinWbasicDatDom, MinHdlrDom, MinMhdlrDom, MinPriDom,
    MinFreeDom, MinRmEvDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, DominoTest, AnyDom);

#define PERF_MEM
// ***********************************************************************************************
TEST(DominoMemTest, GOLD_perf_mem)
{
#ifndef DOMLIB_UT
    GTEST_SKIP() << "env-sensitive benchmark, run only without -Dci";
#endif
    // all typical ops: newEvent + setPrev + setState(true) + setState(false)
    // - libstdc++ string: SSO threshold=15 chars
    // - realistic EvName mix: 50% SSO (≤15 chars, libstdc++ in-object) + 50% heap (≥16 chars)
    const string SHORT_PRE = "short#";                   // "short#N"            ≤15
    const string LONG_PRE  = "long_ev_name_xxxxxxxxx#";  // "long_ev_name_..#N"  ≥16
    auto en = [&](size_t i) {
        return ((i & 1) ? LONG_PRE : SHORT_PRE) + std::to_string(i);
    };

    constexpr size_t N = 100'000;
    using Clock = std::chrono::steady_clock;
    Domino dom;

    // start measure
    auto rss0 = rssBytes();
    auto t0 = Clock::now();

    dom.newEvent(en(0));
    for (size_t i = 1; i < N; ++i)  dom.setPrev(en(i), {{en(i - 1), true}});
    dom.setState({{en(0), true}});
    EXPECT_TRUE(dom.state(en(N - 1)));
    dom.setState({{en(0), false}});
    EXPECT_FALSE(dom.state(en(N - 1)));

    // stop measure
    auto t1 = Clock::now();
    auto rss1 = rssBytes();

    auto msDur = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto totalBytes = (rss1 > rss0) ? (rss1 - rss0) : size_t(0);
    auto bytesPerEv = totalBytes / N;

    // REQ: cost-perf regression guard (fixed docker env)
    // - EvName: reflects real log->gantt workload; all-short ~288B/ev, all-long ~392B/ev
    // - mem: ~344B/ev (mix)  time: ~300ms median for 100K events (-O1)
    //   . TRC overhead ~90ms on 300K calls (~300ns each = snprintf + fwrite)
    //   . vs ostream<<: fwrite 250ms faster (no virtual-call/sentry/locale)
    //   . %s vs %zu in hot-path TRC: measured equal (string ref is free; %zu needs int->str)
    //   . vs no-log: INF removed from hot-path, only TRC remains (~30% of total)
    //   . SSH+docker PTY overhead ~10-20ms (fwrite block-buffered, minimal impact)
    //   . TRACE_OFF=1 env var disables TRC for pure-computation profiling (~190ms)
    EXPECT_LE(bytesPerEv, 380u) << "mem/event=" << bytesPerEv
        << "B, total=" << (totalBytes >> 20) << "MB for " << N << " events";
    EXPECT_LE(msDur, 400) << "time=" << msDur << "ms for " << N << " events";
}

}  // namespace
