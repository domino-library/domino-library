/**
 * Copyright 2019 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gmock/gmock.h>
#include <memory>
#include <set>

#include "UtInitObjAnywhere.hpp"

using std::multiset;
using std::set;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct FreeHdlrDominoTest : public UtParaDom<aParaDom>
{
    MsgCB h1_ = [this](){ hdlrIDs_.insert(1); };
    MsgCB h2_ = [this](){ hdlrIDs_.insert(2); };
    MsgCB h3_ = [this](){ hdlrIDs_.insert(3); };
    MsgCB h4_ = [this](){ hdlrIDs_.insert(4); };
    MsgCB h5_ = [this](){ hdlrIDs_.insert(5); };
    MsgCB h6_ = [this](){ hdlrIDs_.insert(6); };
    MOCK_METHOD(void, h7, ());

    multiset<int> hdlrIDs_;
    set<Domino::Event> uniqueEVs_;
};
TYPED_TEST_SUITE_P(FreeHdlrDominoTest);

// ***********************************************************************************************
template<class aParaDom> using FreeMultiHdlrDominoTest = FreeHdlrDominoTest<aParaDom>;
TYPED_TEST_SUITE_P(FreeMultiHdlrDominoTest);

#define FLAG
// ***********************************************************************************************
TYPED_TEST_P(FreeHdlrDominoTest, GOLD_setFlag_thenGetIt)
{
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(Domino::D_EVENT_FAILED_RET)) << "REQ: invalid event";

    auto e1 = PARA_DOM->newEvent("e1");  // not exist in flag bitmap
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: default flag";

    auto e2 = PARA_DOM->repeatedHdlr("e2");  // explicit set bitmap to true
    EXPECT_TRUE(PARA_DOM->isRepeatHdlr(e2)) << "REQ: get=set";

    PARA_DOM->repeatedHdlr("e2", false);  // explicit set bitmap to false
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e2)) << "REQ: get=set";
}
TYPED_TEST_P(FreeHdlrDominoTest, forbid_setRepeatedFlag_whenHdlrExist)
{
    auto e1 = PARA_DOM->setHdlr("e1", this->h1_);
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->repeatedHdlr("e1", true))
        << "REQ: forbid set repeated flag when hdlr exists";
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: flag not changed";
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, forbid_changeFlag)
{
    auto e1 = PARA_DOM->setHdlr("e1", this->h1_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_");
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1));
    PARA_DOM->repeatedHdlr("e1", true);
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: forbid set when 2 hdlr available";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1"));
    PARA_DOM->repeatedHdlr("e1", true);
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: forbid set when 1 hdlr available";

    EXPECT_TRUE(PARA_DOM->rmOneHdlrOK("e1", "h2_"));
    PARA_DOM->repeatedHdlr("e1", true);
    EXPECT_TRUE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: re-allow change flag when no hdlr";
}

#define AUTO_FREE
// ***********************************************************************************************
TYPED_TEST_P(FreeHdlrDominoTest, GOLD_afterCallback_autoRmHdlr)
{
    auto e1 = PARA_DOM->setHdlr("e1", this->h1_);
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1)) << "REQ: default false (most hdlr used once)";

    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1}), this->hdlrIDs_);  // cb

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1}), this->hdlrIDs_) << "REQ: no more cb since auto-rm";
}
TYPED_TEST_P(FreeHdlrDominoTest, afterCallback_autoRmHdlr_aliasMultiHdlr)
{
    auto aliasE1 = PARA_DOM->setLinkedHdlr("alias e1", this->h3_, "e1");
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(aliasE1));

    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({3}), this->hdlrIDs_);  // cb

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_() ;
    EXPECT_EQ(multiset<int>({3}), this->hdlrIDs_) << "REQ: no more cb since auto-rm";
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, afterCallback_autoRmHdlr_multiHdlr)
{
    auto e1 = PARA_DOM->setHdlr("e1", this->h1_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_");
    auto aliasE1 = PARA_DOM->setLinkedHdlr("alias e1", this->h3_, "e1");
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1));
    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(aliasE1));

    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1, 2, 3}), this->hdlrIDs_);  // cb

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_() ;
    EXPECT_EQ(multiset<int>({1, 2, 3}), this->hdlrIDs_) << "REQ: no more cb since auto-rm";

    // re-add hdlr
    PARA_DOM->setHdlr("alias e1", this->h6_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->h5_, "h2_");
    PARA_DOM->setHdlr("e1", this->h4_);  // reverse order to inc coverage
    this->pongMsgSelf_() ;
    EXPECT_EQ(multiset<int>({1, 2, 3, 4, 5, 6}), this->hdlrIDs_) << "REQ: re-add ok";

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_() ;
    EXPECT_EQ(multiset<int>({1, 2, 3, 4, 5, 6}), this->hdlrIDs_) << "REQ: no more cb since auto-rm";
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, afterCallback_notRmHdlr)
{
    PARA_DOM->repeatedHdlr("e1");  // req: not auto rm; d1 & d2 together
    auto e1 = PARA_DOM->setHdlr("e1", this->h1_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_");
    EXPECT_TRUE(PARA_DOM->isRepeatHdlr(e1));

    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();  // 1st cb
    EXPECT_EQ(multiset<int>({1, 2}), this->hdlrIDs_);

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();  // 2nd cb
    EXPECT_EQ(multiset<int>({1, 2, 1, 2}), this->hdlrIDs_) << "REQ: not auto-rm";

    // re-add hdlr
    PARA_DOM->setHdlr("e1", this->h4_);
    PARA_DOM->multiHdlrOnSameEv("e1", this->h5_, "h2_");

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_() ;
    EXPECT_EQ(multiset<int>({1, 2, 1, 2, 1, 2}), this->hdlrIDs_) << "REQ: re-add nok";
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, BugFix_disorderAutoRm_ok)
{
    PARA_DOM->multiHdlrOnSameEv("e1", this->h3_, "h3_");  // req: will rm before h1_ & h2_/h4_
    PARA_DOM->setState({{"e1", true}});  // h3_ on road
    PARA_DOM->setHdlr("e1", this->h1_);  // h1_ on road
    PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_");  // h2_ on road
    PARA_DOM->multiHdlrOnSameEv("e1", this->h4_, "h4_");  // h4_ on road

    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1, 2, 3, 4}), this->hdlrIDs_);
}
TYPED_TEST_P(FreeHdlrDominoTest, multiCallbackOnRoad_noCrash_noMultiCall)
{
    PARA_DOM->setHdlr("e1", this->h1_);
    PARA_DOM->setState({{"e1", true}});  // 1st on road

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});  // 2nd on road

    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1}), this->hdlrIDs_); // req: no more cb since auto-rm
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, BugFix_multiCallbackOnRoad_noCrash_noMultiCall)
{
    PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_");
    PARA_DOM->setState({{"e1", true}});  // 1st h2_ on road

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});  // 2nd h2_ on road

    PARA_DOM->setHdlr("e1", this->h1_);  // h1_ on road
    PARA_DOM->multiHdlrOnSameEv("e1", this->h3_, "h3_");  // h3_ on road

    this->pongMsgSelf_();
    EXPECT_EQ(multiset<int>({1, 2, 3}), this->hdlrIDs_);  // req: no more cb since auto-rm
}
TYPED_TEST_P(FreeMultiHdlrDominoTest, BugFix_noGapBetween_hdlr_and_autoRm)
{
    PARA_DOM->setPriority("e1", EMsgPri_LOW);  // legacy has gap between hdlr call & autoRm so can't set new hdlr
    auto e1 = PARA_DOM->setHdlr("e1", [this]{ PARA_DOM->setHdlr("e1", this->h1_); });
    PARA_DOM->multiHdlrOnSameEv("e1", [this]{ PARA_DOM->multiHdlrOnSameEv("e1", this->h2_, "h2_"); }, "h2_");

    EXPECT_FALSE(PARA_DOM->isRepeatHdlr(e1));
    PARA_DOM->setState({{"e1", true}});
    EXPECT_EQ(2u, MSG_SELF->nMsg()) << "req: 2 hdlrs on road";

    this->pongMsgSelf_();
    this->pongMsgSelf_();  // each process 1 low hdlr
    EXPECT_EQ(2u, MSG_SELF->nMsg()) << "req: 3 hdlrs called that gen 2 new hdlrs";
    EXPECT_EQ(multiset<int>({}), this->hdlrIDs_) << "1st 3 hdlrs not insert multiset but create new hdlr";

    this->pongMsgSelf_();
    this->pongMsgSelf_();
    EXPECT_EQ(0u, MSG_SELF->nMsg()) << "req: all called";
    EXPECT_EQ(multiset<int>({1, 2}), this->hdlrIDs_) << "REQ: autoRm in time so 2 new hdlrs added succ";
}

#define EXCEPT
// ***********************************************************************************************
TYPED_TEST_P(FreeHdlrDominoTest, except_freeHdlr)
{
    int step = 0;
    PARA_DOM->setHdlr("e1", [&step](){
        step = 1;
        throw std::runtime_error("hdlr except");
    });
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(1, step) << "REQ: FreeHdlrDom shall tolerate except hdlr";

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(1, step) << "REQ: auto-rm still works after except";
}
TYPED_TEST_P(FreeHdlrDominoTest, except_repeatedHdlr)
{
    int step = 0;
    PARA_DOM->repeatedHdlr("e1");
    PARA_DOM->setHdlr("e1", [&step](){
        step++;
        throw std::runtime_error("hdlr except");
    });
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(1, step) << "REQ: repeated hdlr shall tolerate except";

    PARA_DOM->setState({{"e1", false}});
    PARA_DOM->setState({{"e1", true}});
    this->pongMsgSelf_();
    EXPECT_EQ(2, step) << "REQ: repeated hdlr still works after except";
}

#define MEM_LEAK
// ***********************************************************************************************
TYPED_TEST_P(FreeHdlrDominoTest, BugFix_noCrash_whenRmDom)
{
    EXPECT_CALL(*this, h7()).Times(0);
    {
        auto dom = MAKE_PTR<TypeParam>(this->uniLogName());  // local; process dom stays
        dom->setHdlr("e1", [&](){ this->h7(); });
        dom->setState({{"e1", true}});
        ASSERT_TRUE(MSG_SELF->nMsg());
    }  // REQ: no mem leak when rm dom with h7 in msg queue
    this->pongMsgSelf_();
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(FreeHdlrDominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    PARA_DOM->repeatedHdlr("e1");
    PARA_DOM->repeatedHdlr("e1");
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e1"));
    EXPECT_EQ(1u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e1"));

    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);
    EXPECT_EQ(2u, this->uniqueEVs_.size());
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(FreeHdlrDominoTest
    , GOLD_setFlag_thenGetIt
    , forbid_setRepeatedFlag_whenHdlrExist

    , GOLD_afterCallback_autoRmHdlr
    , afterCallback_autoRmHdlr_aliasMultiHdlr
    , multiCallbackOnRoad_noCrash_noMultiCall
    , except_freeHdlr
    , except_repeatedHdlr
    , BugFix_noCrash_whenRmDom

    , nonConstInterface_shall_createUnExistEvent_withStateFalse
);
using AnyFreeDom = Types<MinFreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, FreeHdlrDominoTest, AnyFreeDom);

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(FreeMultiHdlrDominoTest
    , forbid_changeFlag

    , afterCallback_autoRmHdlr_multiHdlr
    , afterCallback_notRmHdlr
    , BugFix_disorderAutoRm_ok
    , BugFix_multiCallbackOnRoad_noCrash_noMultiCall
    , BugFix_noGapBetween_hdlr_and_autoRm
);
using AnyFreeMultiDom = Types<MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, FreeMultiHdlrDominoTest, AnyFreeMultiDom);
}  // namespace
