/**
 * Copyright 2018-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gtest/gtest.h>

#include <utility>

#include "UniLog.hpp"
#include "ObjAnywhere.hpp"

using namespace testing;

namespace rlib
{
// ***********************************************************************************************
struct ObjAnywhereTest : public Test, public UniLog
{
    ObjAnywhereTest() : UniLog(UnitTest::GetInstance()->current_test_info()->name()) {}
    ~ObjAnywhereTest() { GTEST_LOG_FAIL }
};

#define GET_SET
// ***********************************************************************************************
TEST_F(ObjAnywhereTest, GOLD_setThenGetIt)
{
    ObjAnywhere::init(*this);
    auto p1 = MAKE_PTR<int>(1234);
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(p1, *this)) << "REQ: normal set succ";
    EXPECT_EQ(1234, *(ObjAnywhere::getObj<int>().get())) << "REQ: get p1 itself";

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<int>(5678), *this, "i2"));
    EXPECT_EQ(5678,  *(ObjAnywhere::getObj<int >("i2").get())) << "REQ: ok to store same type";
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<bool>("i2").get())  << "REQ: get invalid type -> ret null (shared_ptr failed here)";

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK<int>(nullptr, *this)) << "REQ: set null succ";
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>().get()) << "REQ: get null";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, get_replacement)
{
    ObjAnywhere::init(*this);
    auto p1 = MAKE_PTR<int>(1);
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(p1, *this));
    auto p2 = MAKE_PTR<int>(2);
    EXPECT_FALSE(ObjAnywhere::emplaceObjOK(p2, *this)) << "REQ: replace failed";
    EXPECT_EQ(p1.get(), ObjAnywhere::getObj<int>().get()) << "REQ: refuse replacement";

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK<int>(nullptr, *this));
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(p2, *this));
    EXPECT_EQ(p2.get(), ObjAnywhere::getObj<int>().get()) << "REQ: explicit rm then set ok";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, noSet_getNull)
{
    ObjAnywhere::init(*this);
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>().get()) << "REQ: get null";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, deinit_getNull)
{
    ObjAnywhere::deinit();
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>().get()) << "REQ: get null";
}
TEST_F(ObjAnywhereTest, deinitThenSet_getNull)
{
    ObjAnywhere::deinit();
    EXPECT_FALSE(ObjAnywhere::emplaceObjOK(MAKE_PTR<int>(1234), *this)) << "REQ: no init so set fail";
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>().get()) << "REQ: get null";
}
TEST_F(ObjAnywhereTest, emptyName_getNull)
{
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<int>(42), *this, "validName"));
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>("").get()) << "REQ: empty name -> null";
    EXPECT_FALSE(ObjAnywhere::emplaceObjOK(MAKE_PTR<int>(99), *this, "")) << "REQ: emplace empty name -> fail (OA-1)";
    EXPECT_EQ(42, *(ObjAnywhere::getObj<int>("validName").get())) << "REQ: valid name still works";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, GOLD_defaultName_distinctTypes)
{
    ObjAnywhere::deinit();
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<int >(11), *this)) << "REQ: store int  by default(typeid) name";
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<char>('z'), *this)) << "REQ: store char by default(typeid) name";
    EXPECT_EQ(2u, ObjAnywhere::nObj()) << "REQ: distinct types -> distinct default keys";

    EXPECT_EQ(11 , *(ObjAnywhere::getObj<int >().get())) << "REQ: get int  by default name";
    EXPECT_EQ('z', *(ObjAnywhere::getObj<char>().get())) << "REQ: get char by default name";
    EXPECT_EQ(ObjAnywhere::getObj<int>().get(), ObjAnywhere::getObj<int>().get())
        << "REQ: default name stable across calls (same obj)";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, GOLD_newObjOK_constructInPlace)
{
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::newObjOK<int>(1234))        << "REQ: construct obj in-place by ctor arg (no pre-made ptr)";
    EXPECT_EQ(1234, *(ObjAnywhere::getObj<int>().get())) << "REQ: get constructed obj by default name";
    EXPECT_FALSE(ObjAnywhere::newObjOK<int>(5678))       << "REQ: default name already used -> refuse replace";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, newObjOK_forwardMultiArg)
{
    ObjAnywhere::init(*this);
    EXPECT_TRUE((ObjAnywhere::newObjOK<std::pair<int, int>>(3, 7))) << "REQ: perfect-forward multi ctor args";
    auto p = ObjAnywhere::getObj<std::pair<int, int>>();
    ASSERT_NE(nullptr, p.get());
    EXPECT_EQ(3, p.get()->first);
    EXPECT_EQ(7, p.get()->second) << "REQ: args forwarded in order";
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, newObjOK_noInit_fail)
{
    ObjAnywhere::deinit();
    EXPECT_FALSE(ObjAnywhere::newObjOK<int>(1)) << "REQ: no init -> fail (not crash)";
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<int>().get()) << "REQ: get null";
}

#define CORRECT_DESTRUCT
// ***********************************************************************************************
struct TestObj  // req: ObjAnywhere need not include Obj.hpp
{
    bool& isDestructed_;
    explicit TestObj(bool& aExtFlag) : isDestructed_(aExtFlag) { isDestructed_ = false; }
    ~TestObj() { isDestructed_ = true; }
};
TEST_F(ObjAnywhereTest, noInit_getNull)
{
    ObjAnywhere::deinit();
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<TestObj>().get()) << "REQ: getObj before init";
}
TEST_F(ObjAnywhereTest, GOLD_destructCorrectly)
{
    bool isDestructed;
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<TestObj>(isDestructed), *this));
    EXPECT_FALSE(isDestructed);

    ObjAnywhere::deinit();
    EXPECT_TRUE(isDestructed) << "REQ: destruct correctly";
}
TEST_F(ObjAnywhereTest, destructBySetNull)
{
    bool isDestructed;
    ObjAnywhere::deinit();
    ObjAnywhere::init(*this);

    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(MAKE_PTR<TestObj>(isDestructed), *this));
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(S_PTR<TestObj>(), *this));  // set null
    EXPECT_TRUE(isDestructed) << "REQ: destruct correctly";
    EXPECT_EQ(nullptr, ObjAnywhere::getObj<TestObj>().get()) << "REQ: destruct TestObj";

    EXPECT_FALSE(ObjAnywhere::emplaceObjOK(S_PTR<TestObj>(), *this)) << "REQ: rm again NOK";
    EXPECT_EQ(0u, ObjAnywhere::nObj());

    ObjAnywhere::deinit();
}

#define INIT_DEINIT
// ***********************************************************************************************
TEST_F(ObjAnywhereTest, GOLD_init_deinit)
{
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::isInit());

    ObjAnywhere::deinit();
    EXPECT_FALSE(ObjAnywhere::isInit());
}
TEST_F(ObjAnywhereTest, dup_init_deinit)
{
    ObjAnywhere::init(*this);
    ObjAnywhere::init(*this);
    EXPECT_TRUE(ObjAnywhere::isInit());

    ObjAnywhere::deinit();
    ObjAnywhere::deinit();
    EXPECT_FALSE(ObjAnywhere::isInit());
}
TEST_F(ObjAnywhereTest, ignore_dup_init)
{
    ObjAnywhere::init(*this);
    auto pChar = MAKE_PTR<char>('a');
    EXPECT_TRUE(ObjAnywhere::emplaceObjOK(pChar, *this)) << "REQ: set OK";
    ObjAnywhere::init(*this);  // req: ignore dup init
    EXPECT_EQ(pChar.get(), ObjAnywhere::getObj<char>().get());
    ObjAnywhere::deinit();
}
TEST_F(ObjAnywhereTest, default_is_deinit)
{
    ObjAnywhere::deinit();
    EXPECT_FALSE(ObjAnywhere::isInit());
    EXPECT_EQ(0u, ObjAnywhere::nObj());
}

}  // namespace
