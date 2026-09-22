/**
 * Copyright 2020 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
*/
// ***********************************************************************************************
#include <set>
#include <string>

#include "UniPtr.hpp"
#include "UtInitObjAnywhere.hpp"

using std::string;

namespace rlib
{
// ***********************************************************************************************
template<class aParaDom>
struct DataDominoTest : public UtParaDom<aParaDom>
{
};
TYPED_TEST_SUITE_P(DataDominoTest);

#define SET_GET
// ***********************************************************************************************
const string XPATH_BW =
    "/o-ran-delay-management:delay-management/bandwidth-scs-delay-state[bandwidth=50000][subcarrier-spacing=30]"
    "/bandwidth";
TYPED_TEST_P(DataDominoTest, GOLD_setValue_thenGetIt)
{
    EXPECT_EQ(nullptr, (getData<TypeParam, size_t>(*PARA_DOM, XPATH_BW).get())) << "REQ: for nonexistent event";

    size_t initValue = 50000;
    EXPECT_TRUE((setValueOK<TypeParam, size_t>(*PARA_DOM, XPATH_BW, initValue))) << "REQ: set OK";
    auto valGet = *(getData<TypeParam, size_t>(*PARA_DOM, XPATH_BW).get());
    EXPECT_EQ(initValue, valGet) << "REQ: get = set";

    size_t newValue = 10000;
    EXPECT_TRUE((setValueOK<TypeParam, size_t>(*PARA_DOM, XPATH_BW, newValue))) << "REQ: set OK";
    valGet = *(getData<TypeParam, size_t>(*PARA_DOM, XPATH_BW).get());
    EXPECT_EQ(newValue, valGet) << "REQ: new get = new set";
}
TYPED_TEST_P(DataDominoTest, setShared_thenGetIt_thenRmIt)
{
    EXPECT_EQ(nullptr, PARA_DOM->getData("ev0").get()) << "REQ: get null since ev0 not exist";

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev0", MAKE_PTR<string>("ev0's data"))) << "req: any type data (2nd=string)";
    auto pString = STATIC_PTR_CAST<string>(PARA_DOM->getData("ev0"));  // directly get() will destruct shared_ptr afterward
    ASSERT_NE(nullptr, pString.get());
    EXPECT_EQ("ev0's data", *(pString.get())) << "REQ: get = set";

    *(pString.get()) = "ev0's updated data";
    EXPECT_EQ("ev0's updated data", *(getData<TypeParam, string>(*PARA_DOM, "ev0").get())) << "REQ: get=update";

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev0", MAKE_PTR<string>("replace ev0's data")))
        << "REQ: replace data";
    EXPECT_EQ("replace ev0's data", *(getData<TypeParam, string>(*PARA_DOM, "ev0").get())) << "REQ: get replaced";
    EXPECT_NE(pString.get(), STATIC_PTR_CAST<string>(PARA_DOM->getData("ev0")).get()) << "REQ: replace != old";

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev0")) << "REQ: rm data";
    EXPECT_EQ(nullptr, PARA_DOM->getData("ev0").get()) << "REQ: get null";
}

#define DESTRUCT
// ***********************************************************************************************
TYPED_TEST_P(DataDominoTest, correct_data_destructor)
{
    struct TestData
    {
        bool& isDestructed_;
        explicit TestData(bool& aExtFlag) : isDestructed_(aExtFlag) { isDestructed_ = false; }
        ~TestData() { isDestructed_ = true; }
    };
    bool isDestructed;

    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev", MAKE_PTR<TestData>(isDestructed)))
        << "REQ: any type data (4th=TestData)";
    EXPECT_FALSE(isDestructed);
    EXPECT_TRUE(PARA_DOM->replaceDataOK("ev", nullptr)) << "REQ: remove data";
    EXPECT_TRUE(isDestructed);
}

#define ID_STATE
// ***********************************************************************************************
// event & EvName are ID
// ***********************************************************************************************
TYPED_TEST_P(DataDominoTest, nonConstInterface_shall_createUnExistEvent_withStateFalse)
{
    // DataDomino::
    (void)PARA_DOM->getData("dat-absent");
    EXPECT_EQ(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("dat-absent")) << "REQ: getData does not create";
    EXPECT_EQ(nullptr, PARA_DOM->getData("dat-absent").get()) << "REQ: get null for nonexistent ev";
    EXPECT_FALSE(PARA_DOM->state("dat-absent"));

    EXPECT_TRUE(PARA_DOM->replaceDataOK("e2", MAKE_PTR<int>(0)))  // REQ: any type data (5th=int)
        << "REQ: replace data ok";
    EXPECT_NE(Domino::D_EVENT_FAILED_RET, PARA_DOM->getEventBy("e2")) << "REQ: new Event";
    EXPECT_FALSE(PARA_DOM->state("e2"));
}

// ***********************************************************************************************
REGISTER_TYPED_TEST_SUITE_P(DataDominoTest
    , GOLD_setValue_thenGetIt
    , setShared_thenGetIt_thenRmIt
    , correct_data_destructor
    , nonConstInterface_shall_createUnExistEvent_withStateFalse
);
using AnyDatDom = Types<MinDatDom, MinWbasicDatDom, MaxNofreeDom, MaxDom>;
INSTANTIATE_TYPED_TEST_SUITE_P(PARA, DataDominoTest, AnyDatDom);
}  // namespace
