/**
 * Copyright 2021-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <set>
#include <string>

#include "UtInitObjAnywhere.hpp"

using std::set;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct WbasicDatDomTest : public UtParaDom<aParaDom>
{
    set<Domino::Event> uniqueEVs_;
};
TYPED_TEST_SUITE_P(WbasicDatDomTest);

#define SET_GET
// ***********************************************************************************************
TYPED_TEST_P(WbasicDatDomTest, GOLD_wrCtrl_set_get_rm)  // non-wrData is covered by DataDominoTest
{
    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev0")) << "REQ: write-ctrl data";
    EXPECT_EQ(nullptr, (wbasic_getData<TypeParam, size_t>(*PARA_DOM, "ev0").get())) << "REQ: for non-existed ev0";

    EXPECT_TRUE((wbasic_setValueOK<TypeParam, size_t>(*PARA_DOM, "ev0", 1))) << "REQ: w-set ok";
    auto valGet = *(wbasic_getData<TypeParam, size_t>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(1u, valGet) << "REQ: get = set";

    EXPECT_TRUE((wbasic_setValueOK<TypeParam, size_t>(*PARA_DOM, "ev0", 2))) << "REQ: w-set ok";
    valGet = *(wbasic_getData<TypeParam, size_t>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(2u, valGet) << "REQ: get = update";

    EXPECT_FALSE((setValueOK<TypeParam, size_t>(*PARA_DOM, "ev0", 3))) << "REQ: legacy set failed";
    valGet = *(wbasic_getData<TypeParam, size_t>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(2u, valGet) << "REQ: legacy set failed";

    EXPECT_EQ(nullptr, (getData<TypeParam, size_t>(*PARA_DOM, "ev0").get())) << "REQ: legacy get nonexist";

    EXPECT_FALSE(PARA_DOM->replaceDataOK("ev0")) << "REQ: legacy rm failed";
    valGet = *(wbasic_getData<TypeParam, size_t>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(2u, valGet) << "REQ: legacy rm failed";

    EXPECT_TRUE(PARA_DOM->wbasic_replaceDataOK("ev0")) << "REQ: rm wr-data";
    EXPECT_EQ(nullptr, (PARA_DOM->wbasic_getData("ev0").get())) << "REQ: rm wr-data";
}
TYPED_TEST_P(WbasicDatDomTest, wrCtrlInterface_cannotHdl_nonWrDat)
{
    EXPECT_TRUE((setValueOK<TypeParam, int>(*PARA_DOM, "ev0", 1)))  // req: any type data (2nd=int)
        << "REQ: legacy set ok";
    EXPECT_EQ(nullptr, (wbasic_getData<TypeParam, int>(*PARA_DOM, "ev0").get())) << "REQ: w-get nonexist";

    EXPECT_FALSE((wbasic_setValueOK<TypeParam, int>(*PARA_DOM, "ev0", 2))) << "REQ: w-set failed";
    auto valGet = *(getData<TypeParam, int>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(1, valGet) << "REQ: w-set failed";

    EXPECT_FALSE(PARA_DOM->wbasic_replaceDataOK("ev0")) << "REQ: w-rm failed";
    valGet = *(getData<TypeParam, int>(*PARA_DOM, "ev0").get());
    EXPECT_EQ(1, valGet) << "REQ: w-rm failed";
}
TYPED_TEST_P(WbasicDatDomTest, canNOT_setWriteCtrl_afterOwnData)
{
    EXPECT_TRUE((setValueOK<TypeParam, char>(*PARA_DOM, "ev0", 'a')))  // req: any type data (3rd=char)
        << "REQ: legacy set ok";
    EXPECT_FALSE(PARA_DOM->wrCtrlOk("ev0")) << "REQ: failed to avoid out-ctrl";
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev0")) << "REQ: flag no change";

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev0", nullptr)) << "REQ: remove legacy data";
    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev0")) << "REQ: succ since no data";
    EXPECT_TRUE(PARA_DOM->isWrCtrl("ev0")) << "REQ: flag change";
}

#define FLAG
// ***********************************************************************************************
TYPED_TEST_P(WbasicDatDomTest, setFlag_thenGetIt)
{
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev0")) << "REQ: invalid ev is false";

    PARA_DOM->newEvent("ev0");
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev0")) << "REQ: valid ev is false";

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev0")) << "REQ: set true";
    EXPECT_TRUE(PARA_DOM->isWrCtrl("ev0")) << "REQ: set true";

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev0", false)) << "REQ: set false";
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev0")) << "REQ: set false";

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev1")) << "REQ: create & set true";
    EXPECT_TRUE(PARA_DOM->isWrCtrl("ev1")) << "REQ: create & set true";

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev1")) << "REQ: dup set true";
    EXPECT_TRUE(PARA_DOM->isWrCtrl("ev1")) << "REQ: dup set true";
}
TYPED_TEST_P(WbasicDatDomTest, setFlag_holeWorkWell)
{
    PARA_DOM->newEvent("ev2");
    EXPECT_TRUE(PARA_DOM->wrCtrlOk("ev3")) << "REQ: create wr-ctrl";
    // req: alloc ev3 in bitmap can't impact ev2(still not exist in bitmap)
    EXPECT_TRUE (PARA_DOM->isWrCtrl("ev3"));
    EXPECT_FALSE(PARA_DOM->isWrCtrl("ev2"));  // in bitmap hole
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(WbasicDatDomTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    this->uniqueEVs_.insert(Domino::D_EVENT_FAILED_RET);
    EXPECT_EQ(1u, this->uniqueEVs_.size());

    EXPECT_TRUE(PARA_DOM->wrCtrlOk("e1")) << "REQ: new Event";
    this->uniqueEVs_.insert(PARA_DOM->getEventBy("e1"));
    EXPECT_EQ(2u, this->uniqueEVs_.size());
    EXPECT_FALSE(PARA_DOM->state("e1"));

    EXPECT_EQ(nullptr, (wbasic_getData<TypeParam, char>(*PARA_DOM, "wdat-get-absent").get()))
        << "REQ: no wr-data";
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("wdat-get-absent")) << "REQ: does not create";

    EXPECT_FALSE((wbasic_setValueOK<TypeParam, int>(*PARA_DOM, "wdat-set-absent", 0)))
        << "REQ: no Event since not isWrCtrl";
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("wdat-set-absent")) << "REQ: does not create";
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(WbasicDatDomTest
    , GOLD_wrCtrl_set_get_rm
    , wrCtrlInterface_cannotHdl_nonWrDat
    , canNOT_setWriteCtrl_afterOwnData
    , setFlag_thenGetIt
    , setFlag_holeWorkWell
    , nonConstInterface_shall_createUnExistEvent_withStateFalse
);
using AnyDatDom = Types<MinWbasicDatDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, WbasicDatDomTest, AnyDatDom);

}  // namespace
