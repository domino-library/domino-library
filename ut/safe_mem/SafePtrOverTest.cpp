/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the PolyForm Small Business License 1.0.0
 * SPDX-License-Identifier: PolyForm-Small-Business-1.0.0
 * https://polyformproject.org/licenses/small-business/1.0.0
 * Required Notice: Copyright 2026 Shi-Zhong Chen
 */
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

#include "SafePtrOver.hpp"

using namespace std;

namespace rlib
{
static_assert(sizeof(SafePtrOver<int>) == sizeof(SafePtr<int>), "SafePtrOver<T> is a SafePtr<T>");
static_assert(sizeof(SafePtrOver<void>) == sizeof(SafePtr<void>), "SafePtrOver<void> keeps lastType_");
static_assert(canConstructSafePtrOver<int>::value);
static_assert(canConstructSafePtrOver<const int>::value);
static_assert(canConstructSafePtrOver<std::string>::value);
static_assert(canConstructSafePtrOver<int*>::value);
static_assert(canConstructSafePtrOver<std::unique_ptr<int>>::value);
static_assert(canConstructSafePtrOver<SafePtr<int>>::value);
static_assert(canConstructSafePtrOver<SafePtrOver<int>>::value);
static_assert(canConstructSafePtrOver<std::shared_ptr<int>>::value);
static_assert((canConstructSafePtrOver<std::vector<int*>>::value));
static_assert((canConstructSafePtrOver<std::vector<std::string_view>>::value));
static_assert(!canConstructSafePtrOver<std::string_view>::value);
static_assert(!canConstructSafePtrOver<const std::string_view>::value);
static_assert(!canConstructSafePtrOver<std::initializer_list<int>>::value);

TEST(SafePtrOverTest, payload_is_the_call)
{
    struct Msg { int uid = 1; int* p_ = nullptr; };
    static_assert(canConstructSafePtrOver<Msg>::value);
    EXPECT_TRUE(canConstructSafePtrOver<Msg>::value);
    auto msg = make_over<Msg>();
    ASSERT_TRUE(msg);
    EXPECT_EQ(1, msg.get()->uid);

    struct Self : std::enable_shared_from_this<Self> { int v_ = 1; };
    static_assert(!canConstructSafePtrOver<Self>::value);
    EXPECT_FALSE(canConstructSafePtrOver<Self>::value);

    EXPECT_FALSE(canConstructSafePtrOver<std::initializer_list<int>>::value);
    EXPECT_FALSE(canConstructSafePtrOver<const std::string_view>::value);
}
TEST(SafePtrOverTest, bugFix_makeOver_stringLiteral_copies)
{
    auto text = make_over<std::string>("hi");
    ASSERT_TRUE(text);
    EXPECT_EQ("hi", *text.get());
}

static_assert(std::is_convertible_v<SafePtrOver<int>&, SafePtr<int>>);
static_assert(std::is_convertible_v<SafePtrOver<int>&, const SafePtr<int>&>);
static_assert(!std::is_convertible_v<SafePtrOver<int>&, SafePtr<int>&>);

TEST(SafePtrOverTest, moveOnly_thenShare)
{
    static_assert(!std::is_copy_constructible_v<SafePtrOver<int>>);
    static_assert(!std::is_constructible_v<SafePtrOver<int>, SafePtr<int>>);
    static_assert(!std::is_constructible_v<SafePtrOver<int>, SafePtr<int>&&>);
    static_assert(std::is_constructible_v<SafeWeak<int>, SafePtrOver<int>&>);
    static_assert(std::is_constructible_v<SafeWeak<int>, const SafePtrOver<int>&>);

    auto own = make_over<int>(7);
    ASSERT_TRUE(own);
    EXPECT_EQ(7, *own.get());
    SafePtr<int> copied = own;
    EXPECT_TRUE(own) << "REQ: lvalue conversion keeps the SafePtrOver";
    EXPECT_EQ(2, own.use_count());
    copied = nullptr;
    EXPECT_EQ(1, own.use_count());

    auto moved = std::move(own);
    EXPECT_FALSE(own);
    EXPECT_EQ(7, *moved.get());

    SafePtr<int> sp = std::move(moved);
    EXPECT_FALSE(moved);
    ASSERT_TRUE(sp);
    EXPECT_EQ(1, sp.use_count());
    EXPECT_EQ(7, *sp.get());
    SafeWeak<int> weak(sp);
    EXPECT_FALSE(weak.expired()) << "REQ: SafeWeak starts from the shared result";
    EXPECT_EQ(7, *weak.lock().get());
}

TEST(SafePtrOverTest, share_void_keepsLastType)
{
    SafePtrOver<void> asVoid = make_over<int>(7);
    EXPECT_EQ(type_index(typeid(int)), asVoid.lastType());
    SafePtrOver<void> moved(std::move(asVoid));
    EXPECT_EQ(type_index(typeid(void)), asVoid.lastType()) << "REQ: moved-from void owner is empty";
    EXPECT_EQ(type_index(typeid(int)), moved.lastType());

    auto* self = &moved;
    *self = std::move(moved);  // self-move keeps the value, same as SafePtr
    EXPECT_EQ(type_index(typeid(int)), moved.lastType());
    SafePtr<void> sp = std::move(moved);
    EXPECT_EQ(type_index(typeid(int)), sp.lastType());
    EXPECT_EQ(7, *safe_cast<int>(sp).get());
}

TEST(SafePtrOverTest, const_and_fromThis)
{
    auto c = make_over<const int>(7);
    SafePtr<const int> sp = std::move(c);
    EXPECT_EQ(7, *sp.get());

    struct Self : std::enable_shared_from_this<Self> { int v_ = 1; };
    static_assert(!canConstructSafePtrOver<Self>::value, "detachWeak would leave weak_this on the old control block");
}

TEST(SafePtrOverTest, share_keepsCreatedObject)
{
    struct BaseNV { int x_ = 1; };
    struct DerivedNV : BaseNV
    {
        int* hits_;
        explicit DerivedNV(int& aHits) : hits_(&aHits) {}
        ~DerivedNV() { ++*hits_; }
    };
    int hits = 0;
    {
        auto ele = make_over<DerivedNV>(hits);
        SafePtrOver<BaseNV> asBase(std::move(ele));
        SafePtr<BaseNV> sp = std::move(asBase);
        EXPECT_EQ(0, hits);
        EXPECT_EQ(1, sp.get()->x_);
    }
    EXPECT_EQ(1, hits) << "REQ: ~DerivedNV runs once through SafePtr<BaseNV>";

    struct Imm
    {
        int v_;
        explicit Imm(int aV) : v_(aV) {}
        Imm(const Imm&) = delete;
        Imm(Imm&&) = delete;
    };
    auto imm = make_over<Imm>(5);
    EXPECT_EQ(5, SafePtr<Imm>(std::move(imm)).get()->v_);
}

TEST(SafePtrOverTest, detachWeak_cutsWeak_andRefusesShared)
{
    SafePtrOver<int> empty;
    EXPECT_FALSE(empty.canDetachWeak());
    EXPECT_FALSE(empty.detachWeak());

    auto own = make_over<int>(9);
    std::weak_ptr<int> weak = own.get();
    auto fresh = own.detachWeak();
    EXPECT_FALSE(own) << "REQ: source cleared";
    EXPECT_TRUE(weak.expired()) << "REQ: weak taken from get() expires";
    ASSERT_TRUE(fresh);
    EXPECT_EQ(9, *fresh.get());
    EXPECT_TRUE(fresh.canDetachWeak()) << "REQ: the new owner can detachWeak again";

    auto kept = fresh.get();
    EXPECT_FALSE(fresh.detachWeak()) << "REQ: a stored shared_ptr blocks detachWeak";
    EXPECT_TRUE(fresh) << "REQ: failed detachWeak leaves the SafePtrOver";
    EXPECT_EQ(9, *kept);
}

template<class T, class U, class = void> struct CanMemSwap : std::false_type {};
template<class T, class U> struct CanMemSwap<T, U, std::void_t<decltype(std::declval<T&>().swap(std::declval<U&>()))>> : std::true_type {};

// A SafePtr& cannot bind, so a SafePtr cannot be assigned or swapped into a SafePtrOver.
TEST(SafePtrOverTest, bugFix_swapOrAssignSafePtr_keepsOver)
{
    static_assert(!CanMemSwap<SafePtrOver<int>, SafePtr<int>>::value);
    static_assert(!CanMemSwap<SafePtrOver<int>, SafePtrOver<int>>::value);
    static_assert(!std::is_assignable_v<SafePtrOver<int>&, SafePtr<int>>);
    static_assert(!std::is_assignable_v<SafePtrOver<int>&, SafePtr<int>&&>);

    auto overA = make_over<int>(8);
    auto overB = make_over<int>(9);
    std::swap(overA, overB);
    EXPECT_EQ(9, *overA.get());
    EXPECT_EQ(8, *overB.get());
    EXPECT_TRUE(overA.canDetachWeak()) << "REQ: swap keeps both make_over owners";
    EXPECT_TRUE(overB.canDetachWeak());
}

TEST(SafePtrOverTest, bugFix_safeCast_down_leavesSource)
{
    struct Base {
        virtual ~Base() = default;
        int v_ = 1;
    };
    struct Derive : Base {
        int d_ = 2;
    };
    auto over = make_over<Base>();
    auto bad = safe_cast<Derive>(std::move(over));
    EXPECT_FALSE(bad) << "REQ: a failed downcast does not build a SafePtrOver";
    ASSERT_TRUE(over) << "REQ: a failed cast leaves the source";
    EXPECT_EQ(1, over.get()->v_);
    EXPECT_TRUE(over.canDetachWeak());
}

TEST(SafePtrOverTest, ctorExcept_returnsNull)
{
    struct E {
        E() { throw runtime_error("construct except"); }
    };
    EXPECT_FALSE(make_over<E>());
}

// Identity, not value. Both orders with SafePtr, and both results of == / !=.
TEST(SafePtrOverTest, cmp_sameObject)
{
    auto a = make_over<int>(1);
    auto b = make_over<int>(1);
    SafePtr<int> sp = a;
    SafePtr<int> other = b;
    SafePtrOver<int> empty;
    SafePtr<int> null;

    EXPECT_TRUE(a == a);
    EXPECT_TRUE(a == sp);
    EXPECT_TRUE(sp == a);
    EXPECT_FALSE(a != sp);
    EXPECT_FALSE(sp != a);

    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
    EXPECT_FALSE(a == other);
    EXPECT_TRUE(other != a);
    EXPECT_FALSE(sp == b);
    EXPECT_TRUE(b != sp);

    EXPECT_TRUE(empty == null);
    EXPECT_TRUE(null == empty);
    EXPECT_FALSE(empty != null);
    EXPECT_FALSE(empty == a);
    EXPECT_TRUE(empty != a);
}

}  // namespace
