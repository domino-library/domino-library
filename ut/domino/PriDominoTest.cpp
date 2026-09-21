/**
 * Copyright 2018-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <memory>
#include <queue>
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::queue;
using std::set;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct PriDominoTest : public UtParaDom<aParaDom>
{
    MsgCB d1EventHdlr_ = [&](){ hdlrIDs_.push(1); };
    MsgCB d2EventHdlr_ = [&](){ hdlrIDs_.push(2); };
    MsgCB d3EventHdlr_ = [&](){ hdlrIDs_.push(3); };
    MsgCB d4EventHdlr_ = [&](){ hdlrIDs_.push(4); };
    MsgCB d5EventHdlr_ = [&]()
    {
        hdlrIDs_.push(5);

        ObjAnywhere::getObj<aParaDom>()->setState({{"e2", true}});
        ObjAnywhere::getObj<aParaDom>()->setPriority("e2", EMsgPri_HIGH);
        ObjAnywhere::getObj<aParaDom>()->setHdlr("e2", d2EventHdlr_);          // raise when d5() is exe
    };

    queue<int> hdlrIDs_;
    set<Domino::Event> uniqueEVs_;
};
TYPED_TEST_SUITE_P(PriDominoTest);

// ***********************************************************************************************
template<class aParaDom> using NofreePriDominoTest = PriDominoTest<aParaDom>;    // for no-free testcase
TYPED_TEST_SUITE_P(NofreePriDominoTest);

#define PRI
// ***********************************************************************************************
TYPED_TEST_P(PriDominoTest, setPriority_thenGetIt)
{
    auto event = PARA_DOM->setPriority("event", EMsgPri_HIGH);
    EXPECT_EQ(EMsgPri_HIGH, PARA_DOM->getPriority(event)) << "REQ: get set";

    PARA_DOM->setPriority("event", EMsgPri_LOW);
    EXPECT_EQ(EMsgPri_LOW, PARA_DOM->getPriority(event)) << "REQ: get updated";

    PARA_DOM->setPriority("event", EMsgPri_MAX);
    EXPECT_EQ(EMsgPri_LOW, PARA_DOM->getPriority(event)) << "REQ: invalid set";
}
TYPED_TEST_P(PriDominoTest, defaultPriority)
{
    auto event = PARA_DOM->newEvent("");
    EXPECT_EQ(EMsgPri_NORM, PARA_DOM->getPriority(event)) << "REQ: valid event";

    EXPECT_EQ(EMsgPri_NORM, PARA_DOM->getPriority(Domino::D_EVENT_FAILED_RET)) << "REQ: invalid event";
}
TYPED_TEST_P(PriDominoTest, forbid_changePri)
{
    PARA_DOM->setPriority("e1", EMsgPri_LOW);
    auto e1 = PARA_DOM->setHdlr("e1", this->d1EventHdlr_);
    PARA_DOM->setPriority("e1", EMsgPri_NORM);
    EXPECT_NE(EMsgPri_NORM, PARA_DOM->getPriority(e1)) << "REQ: forbid change pri when hdlr available";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1")) << "REQ: rm hdlr";
    PARA_DOM->setPriority("e1", EMsgPri_NORM);  // EMsgPri_NORM is also to inc cov
    EXPECT_EQ(EMsgPri_NORM, PARA_DOM->getPriority(e1)) << "REQ: allow change pri when no hdlr";
}

#define PRI_FIFO
// ***********************************************************************************************
TYPED_TEST_P(PriDominoTest, GOLD_setPriority_thenPriorityFifoCallback)
{
    PARA_DOM->setState({{"e1", true}});
    PARA_DOM->setHdlr("e1", this->d1EventHdlr_);

    PARA_DOM->setState({{"e5", true}});
    PARA_DOM->setPriority("e5", EMsgPri_HIGH);  // req: higher firstly, & derived callback
    PARA_DOM->setHdlr("e5", this->d5EventHdlr_);

    PARA_DOM->setState({{"e3", true}});
    PARA_DOM->setHdlr("e3", this->d3EventHdlr_);  // req: fifo same priority

    PARA_DOM->setState({{"e4", true}});
    PARA_DOM->setPriority("e4", EMsgPri_HIGH);
    PARA_DOM->setHdlr("e4", this->d4EventHdlr_);

    this->pongMsgSelf_();
    EXPECT_EQ(queue<int>({5, 4, 2, 1, 3}), this->hdlrIDs_);  // auto-rm-hdlr dom
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(PriDominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    PARA_DOM->setPriority("e1", EMsgPri_HIGH);
    PARA_DOM->setPriority("e1", EMsgPri_HIGH);
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e1"));
    EXPECT_EQ(1u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e1"));

    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);
    EXPECT_EQ(2u, this->uniqueEVs_.size());
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(PriDominoTest
    , setPriority_thenGetIt
    , defaultPriority
    , forbid_changePri
    , nonConstInterface_shall_createUnExistEvent_withStateFalse
    , GOLD_setPriority_thenPriorityFifoCallback
);
using AnyPriDom = Types<MinPriDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, PriDominoTest, AnyPriDom);
}  // namespace
