/**
 * Copyright 2024 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <gtest/gtest.h>
#include <cstdlib>
#include <map>
#include <new>
#include <typeindex>
#include <unordered_map>

#include "SafePtr.hpp"

using namespace std;

namespace
{
// Minimal global hook for the one UT that compares make_shared vs new+shared_ptr allocation lifetime.
bool g_probeOn = false;
size_t g_probeMin = 0;
int g_probeAlloc = 0;
int g_probeFree = 0;
void* g_probePtr = nullptr;

void resetBigAllocProbe(size_t aMin) noexcept  // start monitor new/del
{
    g_probeOn = true;
    g_probeMin = aMin;
    g_probeAlloc = g_probeFree = 0;
    g_probePtr = nullptr;
}
void stopBigAllocProbe() noexcept { g_probeOn = false; }  // stop monitor
void onBigAlloc(void* aPtr, size_t aSize) noexcept
{
    if (g_probeOn && aSize >= g_probeMin) {
        ++g_probeAlloc;
        g_probePtr = aPtr;
    }
}
void onBigFree(void* aPtr) noexcept
{
    if (g_probeOn && aPtr == g_probePtr) {
        ++g_probeFree;
        g_probePtr = nullptr;
    }
}
}  // namespace

void* operator new(size_t aSize)
{
    void* p = std::malloc(aSize);
    if (!p) throw std::bad_alloc();
    onBigAlloc(p, aSize);
    return p;
}
void operator delete(void* aPtr) noexcept
{
    onBigFree(aPtr);
    std::free(aPtr);
}
void operator delete(void* aPtr, size_t) noexcept
{
    onBigFree(aPtr);
    std::free(aPtr);
}

namespace rlib
{
// ***********************************************************************************************
// isValid(): centralized invariant check for SafePtr
// - invariant: non-null SafePtr<T≠void> must have lastType=typeid(T)
// - invariant: use_count consistency with bool(ptr)
template<typename T>
testing::AssertionResult isValid(const SafePtr<T>& p)
{
    // bool vs null consistency
    if (bool(p) != (p.get() != nullptr))
        return testing::AssertionFailure() << "REQ: bool(p) shall consist with get()";

    // use_count vs null consistency
    if (bool(p) == (p.use_count() == 0))
        return testing::AssertionFailure() << "REQ: use_count shall consist with bool(p)";

    // lastType & size
    if constexpr(std::is_void_v<T>) {
        EXPECT_EQ(sizeof(SafePtr<T>), sizeof(std::shared_ptr<T>) + sizeof(std::type_index))
            << "REQ: EBO +8B for void only";
        if ((p.lastType() == type_index(typeid(void))) != !p)
            return testing::AssertionFailure() << "REQ: SafePtr<void>.lastType=void only when p=null";
            // impossible legally create SafePtr<void> != null
    } else {
        EXPECT_EQ(sizeof(SafePtr<T>), sizeof(std::shared_ptr<T>)) << "REQ: EBO zero overhead for non-void";
        if (p.lastType() != type_index(typeid(T)))
            return testing::AssertionFailure() << "REQ: correct SafePtr.lastType_=" << typeid(T).name();
    }

    return testing::AssertionSuccess();
}
// EXPECT_VALID(p): gtest prints line#; #p prints variable name — zero manual cost
#define EXPECT_VALID(p) EXPECT_TRUE(isValid(p)) << #p

#define CREATE
// ***********************************************************************************************
TEST(SafePtrTest, GOLD_safeCreate_normal)
{
    SafePtr<int> i = make_safe<int>(42);
    EXPECT_VALID(i);
    EXPECT_EQ(1, i.use_count()) << "REQ: compatible shared_ptr";

    auto content = i.get();
    EXPECT_EQ(42, *content) << "REQ: valid get";
    EXPECT_EQ(2, i.use_count()) << "REQ: compatible shared_ptr";

    *content = 43;
    EXPECT_EQ(43, *i.get()) << "REQ: valid update";

    content = nullptr;
    EXPECT_EQ(nullptr, content) << "REQ: reset OK";
    EXPECT_EQ(43, *i.get()) << "REQ: outside reset not impact SafePtr";
    EXPECT_EQ(1, i.use_count()) << "REQ: compatible shared_ptr";
    EXPECT_VALID(i);
}
TEST(SafePtrTest, GOLD_unsafeCreate_forbid)
{
    // SafePtr<int> ptr(raw);  // compile-err
    static_assert(!is_constructible<SafePtr<int>, int*>::value,
        "REQ: forbid construct: raw pointer is unsafe");

    // SafePtr<int> ptr(make_shared<int>());  // compile-err
    static_assert(!is_constructible<SafePtr<int>, shared_ptr<int>>::value,
        "REQ: forbid construct: shared_ptr is unsafe - that's why SafePtr exists");
}
TEST(SafePtrTest, safeCreate_default)
{
    SafePtr v;
    EXPECT_VALID(v);

    SafePtr<int> i;
    EXPECT_VALID(i);

    auto ptr = make_safe<int>();
    EXPECT_EQ(0, *ptr.get()) << "REQ: empty make_safe like make_shared";
    EXPECT_EQ(1, ptr.use_count()) << "REQ: compatible shared_ptr";
    EXPECT_VALID(ptr);
}
TEST(SafePtrTest, safeCreate_null)
{
    const SafePtr v = nullptr;  // common
    EXPECT_VALID(v);

    const SafePtr<int> i = nullptr;
    EXPECT_VALID(i);
}

#define CAST
// ***********************************************************************************************
struct Base                 { virtual int value() const  { return 0; } };
struct Derive : public Base { int value() const override { return 1; } };

// shared helpers — reuse across tests to keep SafePtr<T> instantiations bounded
// (gcovr 8.x counts each instantiation's branches separately)
struct BB { int i = 0; };  // non-virtual base for slicing tests
struct DD : BB { DD() { i = 1; } };

struct MiB1 { virtual ~MiB1() = default; int b1 = 0; };  // MI tests: 2 virtual bases
struct MiB2 { virtual ~MiB2() = default; int b2 = 0; };
struct MiD : MiB1, MiB2 { MiD() { b1 = 11; b2 = 22; } };

TEST(SafePtrTest, GOLD_safeCast)
{
    auto d = make_safe<Derive>();
    EXPECT_VALID(d);
    auto nRef = d.use_count();

    // cast to self
    auto dd = safe_cast<Derive>(d);
    EXPECT_VALID(dd);
    EXPECT_EQ(1, dd->value())        << "REQ: Derive->Derive";
    EXPECT_VALID(d);
    EXPECT_EQ(++nRef, d.use_count()) << "REQ: all are shared";

    auto dbd = safe_cast<Derive>(safe_cast<Base>(d));
    EXPECT_VALID(dbd);
    EXPECT_EQ(1, dbd->value())       << "REQ: Derive->Base->Derive";
    EXPECT_VALID(d);
    EXPECT_EQ(++nRef, d.use_count()) << "REQ: all are shared";

    auto dbvbd = safe_cast<Derive>(safe_cast<Base>(safe_cast<void>(safe_cast<Base>(d))));
    EXPECT_VALID(dbvbd);
    EXPECT_EQ(1, dbvbd->value())     << "REQ: Derive->Base->void->Base->Derive";
    EXPECT_VALID(d);
    EXPECT_EQ(++nRef, d.use_count()) << "REQ: all are shared";

    auto dbdvd = safe_cast<Derive>(safe_cast<void>(safe_cast<Derive>(safe_cast<Base>(d))));
    EXPECT_VALID(dbdvd);
    EXPECT_EQ(1, dbdvd->value())     << "REQ: Derive->Base->Derive->void->Derive";
    EXPECT_VALID(d);
    EXPECT_EQ(++nRef, d.use_count()) << "REQ: all are shared";

    // cast to const
    auto dcb = safe_cast<const Base>(d);
    EXPECT_VALID(dcb);
    EXPECT_EQ(1, dcb->value())       << "REQ: Derive->const Base (virtual dispatch)";
    EXPECT_EQ(++nRef, d.use_count()) << "REQ: all are shared";

    // cast nullptr
    auto n = safe_cast<Base>(SafePtr<Derive>());
    EXPECT_VALID(n);
    EXPECT_EQ(nullptr, n.get())      << "REQ: cast nullptr -> nullptr";

    auto dvb = safe_cast<Base>(safe_cast<void>(d));
    EXPECT_VALID(dvb);
    EXPECT_EQ(nullptr, dvb.get())    << "Derive->void->Base: unsupported since mem & impl cost";
    EXPECT_VALID(d);
    EXPECT_EQ(nRef, d.use_count()) << "REQ: keep shared";

    auto dbvd = safe_cast<Derive>(safe_cast<void>(safe_cast<Base>(d)));
    EXPECT_VALID(dbvd);
    EXPECT_EQ(nullptr, dbvd.get())   << "Derive->Base->void->Derive unsupported";
    EXPECT_VALID(d);
    EXPECT_EQ(nRef, d.use_count()) << "REQ: keep shared";
}
TEST(SafePtrTest, GOLD_unsafeCast)
{
    //EXPECT_EQ(nullptr, safe_cast<char>(make_safe<int>(7)).get());  // unsafe, compile err safer than ret nullptr
    //EXPECT_EQ(nullptr, safe_cast<char>(SafePtr  <int>( )).get());  // unsafe, same as dynamic_pointer_cast
    EXPECT_EQ(nullptr, safe_cast<Derive>(make_safe<Base>() ).get()) << "REQ: unsafe Base->Derive";

    struct D_protect : protected Derive { int value() const override { return 2; } };
    struct D_private : private   Derive { int value() const override { return 3; } };
    //EXPECT_EQ(nullptr, safe_cast<Base>(make_safe<D_private>()).get());  // unsafe, compile err
    //EXPECT_EQ(nullptr, safe_cast<Base>(make_safe<D_protect>()).get());  // unsafe, compile err

    auto d = safe_cast<BB>(make_safe<DD>());
    EXPECT_VALID(d);
    EXPECT_EQ(1, d.get()->i) << "D sliced to B but still safe";
    //EXPECT_EQ(nullptr, safe_cast<DD>(d).get());  // unsafe, compile err

    auto msgInQ  = SafePtr<void>(SafePtr<Base>(make_safe<Derive>()));
    auto msgOutQ = safe_cast<char>(msgInQ);  // failed cast
    EXPECT_EQ(nullptr                 , msgOutQ.get     ()) << "REQ: failed cast -> dest get nothing";
    EXPECT_EQ(type_index(typeid(char)), msgOutQ.lastType()) << "REQ: failed cast -> dest get nothing";
    EXPECT_VALID(msgInQ);
    EXPECT_VALID(msgOutQ);

    //auto x = safe_cast<Base>(make_safe<const Base>());  // unsafe const->non-const, compile err
}
TEST(SafePtrTest, bugFix_safeCast_multiVoid)
{
    auto dbvvbd = safe_cast<Base>(safe_cast<Derive>(safe_cast<Base>(
        safe_cast<void>(safe_cast<void>(  // multi void
            safe_cast<Base>(make_safe<Derive>()))))));
    EXPECT_EQ(1, dbvvbd->value()) << "REQ: multi void OK";
    EXPECT_VALID(dbvvbd);
}
TEST(SafePtrTest, GOLD_safe_D_B_void_B)
{
    // Multiple inheritance: D has two bases at different offsets
    struct A { virtual int a() { return 1; } };
    struct B { virtual int b() { return 2; } };
    struct D : A, B {
        int a() override { return 11; }
        int b() override { return 22; }
    };

    auto d = make_safe<D>();
    EXPECT_EQ(sizeof(d), sizeof(shared_ptr<D>)) << "REQ: EBO for SafePtr<D>";

    // D->B
    SafePtr<B> b = d;
    EXPECT_VALID(b);
    EXPECT_EQ(22, b->b()) << "REQ: correct B sub-object";
    EXPECT_EQ(sizeof(b), sizeof(shared_ptr<B>)) << "REQ: EBO for SafePtr<B>";

    // B->void
    SafePtr<void> v = b;
    EXPECT_VALID(v);
    EXPECT_EQ(sizeof(v), (sizeof(shared_ptr<B>) + sizeof(std::type_index))) << "REQ: EBO for SafePtr<void>";

    // void→D
    auto invalidD = safe_cast<D>(v);
    EXPECT_VALID(invalidD);
    EXPECT_EQ(nullptr, invalidD.get()) << "REQ: void→D not possible when lastType=B";
    EXPECT_EQ(sizeof(invalidD), sizeof(shared_ptr<D>)) << "REQ: EBO for SafePtr<D>";

    // void->B
    auto backB = safe_cast<B>(v);
    EXPECT_VALID(backB);
    EXPECT_EQ(22, backB->b()) << "REQ: correct B sub-object";
    EXPECT_EQ(sizeof(backB), sizeof(shared_ptr<B>)) << "REQ: EBO for SafePtr<B>";

    // B->D
    auto backD = safe_cast<D>(b);
    EXPECT_VALID(backD);
    EXPECT_EQ(11, backD->a()) << "REQ: correct D sub-object";
    EXPECT_EQ(22, backD->b()) << "REQ: correct D sub-object";
    EXPECT_EQ(sizeof(backD), sizeof(shared_ptr<D>)) << "REQ: EBO for SafePtr<D>";
}
TEST(SafePtrTest, GOLD_safe_virtualDiamond)
{
    // virtual diamond: D inherits L,R virtually from B; only ONE B sub-object exists in D.
    // - non-virtual MI would give 2 B sub-objects → D*→B* ambiguous (compile-err)
    // - virtual inheritance resolves ambiguity but adds vbase offset (runtime adjust)
    // REQ: SafePtr's safe-conversion & safe_cast must work over virtual-base offsets,
    //      and round-trip via void must restore the SAME single B sub-object.
    struct VB { virtual ~VB() = default; int v = 0; virtual int who() { return 0; } };
    struct VL : virtual VB { VL() { v = 7; }       int who() override { return 1; } };
    struct VR : virtual VB {                       int who() override { return 2; } };
    struct VD : VL, VR     { VD() { v = 9; }       int who() override { return 3; } };

    auto d = make_safe<VD>();
    EXPECT_VALID(d);
    EXPECT_EQ(9, d->v);

    // D→L, D→R: implicit safe cp; both must reach the SAME single B sub-object
    SafePtr<VL> l = d;
    SafePtr<VR> r = d;
    EXPECT_VALID(l);
    EXPECT_VALID(r);
    EXPECT_EQ(9, l->v) << "REQ: virtual-base shared via L";
    EXPECT_EQ(9, r->v) << "REQ: virtual-base shared via R";

    // D→B (unambiguous because virtual): same single B sub-object
    SafePtr<VB> bFromL = l;
    SafePtr<VB> bFromR = r;
    EXPECT_VALID(bFromL);
    EXPECT_VALID(bFromR);
    EXPECT_EQ(bFromL.get().get(), bFromR.get().get()) << "REQ: virtual diamond -> one shared B sub-object";
    EXPECT_EQ(3, bFromL->who()) << "REQ: virtual dispatch reaches D::who()";

    // round-trip via void: B→void→B preserves the same B address (vbase offset already applied)
    SafePtr<void> v = bFromL;
    auto backB = safe_cast<VB>(v);
    EXPECT_VALID(backB);
    EXPECT_EQ(bFromL.get().get(), backB.get().get()) << "REQ: void round-trip keeps same B sub-object";
    EXPECT_EQ(3, backB->who());

    // shared ownership across all views: d + l + r + bFromL + bFromR + v + backB = 7
    EXPECT_EQ(7u, d.use_count()) << "REQ: all views share ownership";
}

#define COPY
// ***********************************************************************************************
struct A
{
    int value() { return 1; }
    int value() const { return 0; }
};
TEST(SafePtrTest, GOLD_safeCp_toSameType)
{
    auto i = make_safe<int>(42);
    SafePtr<int> i2(i);  // cp, not mv
    EXPECT_EQ(42, *i2.get())    << "REQ: cp to self";
    EXPECT_EQ(2, i.use_count()) << "REQ: shared ownership";
    EXPECT_VALID(i);
    EXPECT_VALID(i2);

    auto a = make_safe<A>();
    SafePtr<const A> ac(a);
    EXPECT_EQ(0, ac->value()) << "REQ: cp to const";
    EXPECT_VALID(a);
    EXPECT_VALID(ac);

    const SafePtr<A> ca(a);
    EXPECT_EQ(1, ca->value()) << "REQ: cp ok & calls non-const ok";
    EXPECT_VALID(a);
    EXPECT_VALID(ca);

    const SafePtr<Base> cb = make_safe<Base>();
    EXPECT_EQ(0, SafePtr<Base>(cb)->value()) << "REQ: safe cp from const to non-const - like shared_ptr";
    EXPECT_VALID(cb);
}
TEST(SafePtrTest, GOLD_safeCp_toVoid)
{
    auto db = SafePtr<Base>(make_safe<Derive>());
    SafePtr<void> dbv(db);
    // check target
    EXPECT_EQ(1,            safe_cast<Base>(dbv)->value()) << "req: cp Derive->Base->void->Base";
    EXPECT_EQ(type_index(typeid(Base)),    dbv.lastType()) << "REQ: cp to void preserves lastType=Base";
    // check src
    EXPECT_EQ(2,                           db.use_count()) << "REQ: cp is shared";
    EXPECT_EQ(1,             safe_cast<Base>(db)->value()) << "REQ: keep src after cp";
    EXPECT_EQ(type_index(typeid(Base)),    db.lastType ()) << "REQ: keep src after cp";

    auto dbd = safe_cast<Derive>(db);
    SafePtr<void> dbdv(dbd);
    // check target
    EXPECT_EQ(1,         safe_cast<Derive>(dbdv)->value()) << "req: cp Derive->Base->Derive->void->Derive";
    EXPECT_EQ(type_index(typeid(Derive)), dbdv.lastType()) << "REQ: cp to void preserves lastType=Derive";
    // check src
    EXPECT_EQ(4,                          dbd.use_count()) << "REQ: cp is shared";
    EXPECT_EQ(1,          safe_cast<Base>(dbd)->value  ()) << "REQ: keep src after cp";
    EXPECT_EQ(type_index(typeid(Derive)), dbd.lastType ()) << "REQ: keep src after cp";
    EXPECT_VALID(dbv);
    EXPECT_VALID(dbdv);
}
TEST(SafePtrTest, GOLD_safeCp_toPolyBase)
{
    auto d = make_safe<Derive>();
    SafePtr<Base> db(d);
    // check target
    EXPECT_EQ(1,                          db->value  ()) << "REQ: cp to Base";
    EXPECT_EQ(type_index(typeid(Base)),   db.lastType()) << "REQ: lastType=Base";
    // check src
    EXPECT_EQ(2,                          d.use_count()) << "REQ: cp is shared";
    EXPECT_EQ(1,                          d->value   ()) << "REQ: keep src after cp";
    EXPECT_EQ(type_index(typeid(Derive)), d.lastType ()) << "REQ: keep src after cp";

    SafePtr<const Base> dbc(d);
    // check target
    EXPECT_EQ(1,                              dbc->value  ()) << "REQ: cp to const Base";
    EXPECT_EQ(type_index(typeid(const Base)), dbc.lastType()) << "REQ: lastType=const Base";
    // check src
    EXPECT_EQ(3,                          d.use_count()) << "REQ: cp is shared";
    EXPECT_EQ(1,                          d->value   ()) << "REQ: keep src after cp";
    EXPECT_EQ(type_index(typeid(Derive)), d.lastType ()) << "req: keep src after cp";

    EXPECT_VALID(d);
    EXPECT_VALID(db);
    EXPECT_VALID(dbc);
}
TEST(SafePtrTest, safeCp_toStaticBase)
{
    auto d = make_safe<DD>();
    SafePtr<BB> b(d);
    EXPECT_VALID(b);
    EXPECT_EQ(1, b->i) << "REQ: cp to non-virtual base";
    EXPECT_EQ(2, d.use_count()) << "REQ: shared";
}
TEST(SafePtrTest, safeCp_nullptr)
{
    auto d = SafePtr<Derive>();
    SafePtr<void> dv(d);
    EXPECT_EQ(nullptr, dv.get());
    EXPECT_EQ(type_index(typeid(void)), dv.lastType ()) << "REQ: cp nullptr as if cp fail, avoid confuse usr";
    EXPECT_EQ(0,                        dv.use_count()) << "REQ: none";
    EXPECT_EQ(0,                        d .use_count()) << "REQ: none";
    EXPECT_VALID(dv);
}
struct D_private   : private   Base {};
struct D_protected : protected Base {};
TEST(SafePtrTest, unsafeCpMvAssign_compileForbid)
{
    // diff type
    static_assert(!is_constructible<SafePtr<char>, const SafePtr<int>&>::value,
        "REQ: forbid cp diff type");
    static_assert(!is_constructible<SafePtr<char>, SafePtr<int>&&>::value,
        "REQ: forbid mv diff type");

    // private/protected inherit
    static_assert(!is_constructible<SafePtr<Base>, const SafePtr<D_private>&>::value,
        "REQ: forbid cp private-derive to base");
    static_assert(!is_constructible<SafePtr<Base>, SafePtr<D_private>&&>::value,
        "REQ: forbid mv private-derive to base");
    static_assert(!is_constructible<SafePtr<Base>, const SafePtr<D_protected>&>::value,
        "REQ: forbid cp protected-derive to base");
    static_assert(!is_constructible<SafePtr<Base>, SafePtr<D_protected>&&>::value,
        "REQ: forbid mv protected-derive to base");

    // base to derive
    static_assert(!is_constructible<SafePtr<Derive>, const SafePtr<Base>&>::value,
        "REQ: forbid cp base to derive");
    static_assert(!is_constructible<SafePtr<Derive>, SafePtr<Base>&&>::value,
        "REQ: forbid mv base to derive");

    // void to non-void
    static_assert(!is_constructible<SafePtr<Base>, const SafePtr<void>&>::value,
        "REQ: forbid cp void to non-void");
    static_assert(!is_constructible<SafePtr<Base>, SafePtr<void>&&>::value,
        "REQ: forbid mv void to non-void");

    // const to non-const
    static_assert(!is_constructible<SafePtr<Base>, const SafePtr<const Base>&>::value,
        "REQ: forbid cp const to non-const");
    static_assert(!is_constructible<SafePtr<Base>, SafePtr<const Base>&&>::value,
        "REQ: forbid mv const to non-const");

    // assign: same forbids apply (cross-type = uses ctor + mv=)
    static_assert(!is_assignable<SafePtr<char>&, const SafePtr<int>&>::value,
        "REQ: forbid assign diff type");
    static_assert(!is_assignable<SafePtr<Base>&, const SafePtr<D_private>&>::value,
        "REQ: forbid assign private-derive to base");
    static_assert(!is_assignable<SafePtr<Base>&, const SafePtr<D_protected>&>::value,
        "REQ: forbid assign protected-derive to base");
    static_assert(!is_assignable<SafePtr<Derive>&, const SafePtr<Base>&>::value,
        "REQ: forbid assign base to derive");
    static_assert(!is_assignable<SafePtr<Base>&, const SafePtr<void>&>::value,
        "REQ: forbid assign void to non-void");
    static_assert(!is_assignable<SafePtr<Base>&, const SafePtr<const Base>&>::value,
        "REQ: forbid assign const to non-const");
}

#define MOVE
// ***********************************************************************************************
TEST(SafePtrTest, GOLD_safeMv_toSameType)
{
    auto i = make_safe<int>(42);
    SafePtr<int> i2(move(i));
    EXPECT_VALID(i);
    EXPECT_VALID(i2);
    EXPECT_EQ(42, *i2.get())     << "REQ: mv to self";
    EXPECT_EQ(1, i2.use_count()) << "REQ: mv ownership";
    EXPECT_EQ(nullptr, i.get())  << "REQ: source is empty after move";

    auto a = make_safe<A>();
    SafePtr<const A> ac(move(a));
    EXPECT_VALID(a);
    EXPECT_VALID(ac);
    EXPECT_EQ(0, ac->value()) << "REQ: mv to const";
    EXPECT_EQ(nullptr, a.get())  << "REQ: source is empty after mv";
}
TEST(SafePtrTest, GOLD_safeMv_toVoid)
{
    auto db = SafePtr<Base>(make_safe<Derive>());
    SafePtr<void> dbv(move(db));
    EXPECT_VALID(db);
    EXPECT_VALID(dbv);
    // check target
    EXPECT_EQ(1,         safe_cast<Base>(dbv)->value()) << "req: mv Derive->Base->void->Base";
    EXPECT_EQ(type_index(typeid(Base)), dbv.lastType()) << "REQ: mv to void preserves lastType=Base";
    // check src
    EXPECT_EQ(nullptr,                  db.get      ()) << "REQ: reset src after mv";
    EXPECT_EQ(type_index(typeid(Base)), db.lastType ()) << "REQ: reset src after mv";

    auto dbd = safe_cast<Derive>(SafePtr<Base>(make_safe<Derive>()));
    SafePtr<void> dbdv(move(dbd));
    EXPECT_VALID(dbd);
    EXPECT_VALID(dbdv);
    // check target
    EXPECT_EQ(1,         safe_cast<Derive>(dbdv)->value()) << "req: mv Derive->Base->Derive->void->Derive";
    EXPECT_EQ(type_index(typeid(Derive)), dbdv.lastType()) << "REQ: mv to void preserves lastType=Derive";
    // check src
    EXPECT_EQ(nullptr,                    dbd.get      ()) << "REQ: reset src after mv";
    EXPECT_EQ(type_index(typeid(Derive)), dbd.lastType ()) << "REQ: reset src after mv";
}
TEST(SafePtrTest, GOLD_safeMv_toPolyBase)
{
    auto d = make_safe<Derive>();
    SafePtr<Base> db(move(d));
    EXPECT_VALID(d);
    EXPECT_VALID(db);
    // check target
    EXPECT_EQ(1,             SafePtr<Base>(db)->value()) << "REQ: mv to Base";
    EXPECT_EQ(type_index(typeid(Base  )), db.lastType()) << "REQ: lastType=Base";
    // check src
    EXPECT_EQ(nullptr,                    d.get      ()) << "REQ: reset src after mv";
    EXPECT_EQ(type_index(typeid(Derive)), d.lastType ()) << "REQ: reset src after mv";

    SafePtr<const Base> dbc(move(db));
    EXPECT_VALID(db);
    EXPECT_VALID(dbc);
    // check target
    EXPECT_EQ(1,                              dbc->value  ()) << "REQ: mv to const Base";
    EXPECT_EQ(type_index(typeid(const Base)), dbc.lastType()) << "REQ: lastType=const Base";
    // check src
    EXPECT_EQ(nullptr,                  db.get      ()) << "req: reset src after mv";
    EXPECT_EQ(type_index(typeid(Base)), db.lastType ()) << "REQ: reset src after mv";
}
TEST(SafePtrTest, safeMv_toStaticBase)
{
    auto d = make_safe<DD>();
    SafePtr<BB> b(move(d));
    EXPECT_VALID(d);
    EXPECT_VALID(b);
    EXPECT_EQ(1, b->i) << "REQ: mv to non-virtual base";
    EXPECT_EQ(nullptr, d.get()) << "REQ: src empty after mv";
}

#define ASSIGN
// ***********************************************************************************************
TEST(SafePtrTest, GOLD_safe_assign)  // operator=() is auto-gen, just simple test 1 case
{
    SafePtr<int> one;
    auto two = make_safe<int>(42);
    one = two;
    EXPECT_EQ(42, *(one.get())) << "REQ: valid assign & get";

    two = SafePtr<int>();
    EXPECT_EQ(nullptr, two.get()) << "REQ: assign to null";
    EXPECT_EQ(42, *(one.get())) << "REQ: valid get after assigner is reset";
    EXPECT_VALID(one);
    EXPECT_VALID(two);

    // same-type mv=
    auto three = make_safe<int>(99);
    SafePtr<int> four;
    four = move(three);
    EXPECT_VALID(three);
    EXPECT_VALID(four);
    EXPECT_EQ(99, *four.get()) << "REQ: mv= transfers value";
    EXPECT_EQ(nullptr, three.get()) << "REQ: src empty after mv=";
}
TEST(SafePtrTest, safeAssign_void)
{
    SafePtr<void> v1(make_safe<int>(42));
    SafePtr<void> v2;
    v2 = v1;  // void cp=
    EXPECT_VALID(v1);
    EXPECT_VALID(v2);
    EXPECT_EQ(type_index(typeid(int)), v2.lastType()) << "REQ: void cp= preserves lastType";
    EXPECT_EQ(v1.get(), v2.get()) << "REQ: shared ownership";
}
TEST(SafePtrTest, safeAssign_crossType)
{
    auto d = make_safe<Derive>();
    SafePtr<Base> b;
    b = d;  // Derive→Base assign
    EXPECT_VALID(b);
    EXPECT_EQ(1, b->value()) << "REQ: cross-type assign";
    EXPECT_EQ(2, d.use_count()) << "REQ: shared";

    SafePtr<void> v;
    v = SafePtr<Base>(d);  // Base→void assign
    EXPECT_VALID(v);
    EXPECT_EQ(type_index(typeid(Base)), v.lastType()) << "REQ: assign to void preserves lastType";
}
TEST(SafePtrTest, GOLD_safeReset_byAssign)
{
    auto msgInQ = SafePtr<void>(SafePtr<Base>(make_safe<Derive>()));
    EXPECT_NE(nullptr                 , msgInQ.get      ()) << "REQ: init ok";
    EXPECT_EQ(type_index(typeid(Base)), msgInQ.lastType ()) << "REQ: lastType=Base";

    msgInQ = nullptr;  // reset
    EXPECT_EQ(nullptr                 , msgInQ.get      ()) << "REQ: reset ok";
    EXPECT_EQ(type_index(typeid(void)), msgInQ.lastType ()) << "REQ: reset ok";
    EXPECT_VALID(msgInQ);
}
TEST(SafePtrTest, GOLD_safeSwap)
{
    auto one = make_safe<int>(1);
    auto two = make_safe<int>(2);

    one.swap(two);
    EXPECT_VALID(one);
    EXPECT_VALID(two);
    EXPECT_EQ(2, *one.get()) << "REQ: member swap exchanges ownership";
    EXPECT_EQ(1, *two.get()) << "REQ: member swap exchanges ownership";

    swap(one, two);
    EXPECT_VALID(one);
    EXPECT_VALID(two);
    EXPECT_EQ(1, *one.get()) << "REQ: free swap exchanges ownership";
    EXPECT_EQ(2, *two.get()) << "REQ: free swap exchanges ownership";
}
TEST(SafePtrTest, GOLD_safeSwap_void)
{
    // void swap must exchange both ctrl-block AND lastType_ (else safe_cast back fails)
    SafePtr<void> vi = make_safe<int>(42);
    SafePtr<void> vf = make_safe<float>(3.14f);

    vi.swap(vf);
    EXPECT_EQ(type_index(typeid(float)), vi.lastType()) << "REQ: void swap exchanges lastType";
    EXPECT_EQ(type_index(typeid(int  )), vf.lastType()) << "REQ: void swap exchanges lastType";
    EXPECT_EQ(42, *safe_cast<int>(vf).get()) << "REQ: lastType moved with ctrl-block, cast still safe";
}

#define DESTRUCT
// ***********************************************************************************************
struct TestBase
{
    bool& isBaseOver_;
    explicit TestBase(bool& aExtFlag) : isBaseOver_(aExtFlag) { isBaseOver_ = false; }
    virtual ~TestBase() { isBaseOver_ = true; }
};
TEST(SafePtrTest, GOLD_destructByVoid)
{
    bool isBaseOver;
    SafePtr<void> test = make_safe<TestBase>(isBaseOver);
    EXPECT_FALSE(isBaseOver) << "correctly constructed";

    test = nullptr;
    EXPECT_VALID(test);
    EXPECT_TRUE(isBaseOver) << "REQ: correctly destructed by SafePtr<void>";
}
struct TestDerive : public TestBase
{
    bool& isDeriveOver_;
    explicit TestDerive(bool& aBaseFlag, bool& aDeriveFlag)
        : TestBase(aBaseFlag)
        , isDeriveOver_(aDeriveFlag)
    { isDeriveOver_ = false; }
    ~TestDerive() { isDeriveOver_ = true; }
};
TEST(SafePtrTest, GOLD_destructByBase)
{
    bool isBaseOver;
    bool isDeriveOver;
    SafePtr<TestBase> test = make_safe<TestDerive>(isBaseOver, isDeriveOver);
    EXPECT_FALSE(isDeriveOver) << "correctly constructed";

    test = nullptr;
    EXPECT_VALID(test);
    EXPECT_TRUE(isDeriveOver) << "REQ: correctly destructed by SafePtr<TestBase>";
}

#define LIKE_SHARED_PTR
// ***********************************************************************************************
TEST(SafePtrTest, get_isMemSafe_afterDelOrigin)
{
    SafePtr<string> safe = make_safe<string>("hello");
    auto get = safe.get();
    EXPECT_EQ("hello", *get) << "get succ";

    safe = nullptr;
    EXPECT_EQ("hello", *get) << "REQ: what got is still OK";

    get = safe.get();
    EXPECT_EQ(nullptr, get) << "REQ: get nullptr OK";
}
TEST(SafePtrTest, GOLD_stlPointerTraits)
{
    static_assert(is_same<pointer_traits<SafePtr<int>>::element_type, int>::value,
        "REQ: std::pointer_traits can query SafePtr element_type");
    static_assert(is_same<pointer_traits<SafeWeak<int>>::element_type, int>::value,
        "REQ: std::pointer_traits can query SafeWeak element_type");
    SUCCEED() << "REQ: smart-pointer metadata is compile-time visible to STL traits";
}

#define SUPPORT_MAP
// ***********************************************************************************************
TEST(SafePtrTest, GOLD_asKeyOf_unorderedMap)
{
    unordered_map<SafePtr<string>, int> store;
    EXPECT_EQ(0, store.size()) << "REQ: SafePtr can be key";

    store[SafePtr<string>()] = 100;
    EXPECT_EQ(nullptr, SafePtr<string>().get());
    EXPECT_EQ(1, store.size()) << "REQ: key=nullptr is OK";

    store[SafePtr<string>()] = 200;
    EXPECT_EQ(1,   store.size()) << "REQ: no dup key";
    EXPECT_EQ(200, store[SafePtr<string>()]) << "REQ: dup key overwrites value";

    store[SafePtr<string>(make_safe<string>("hello"))] = 300;
    EXPECT_EQ(2, store.size()) << "REQ: diff key";
}
TEST(SafePtrTest, GOLD_asKeyOf_map)
{
    map<SafePtr<string>, int> store;
    EXPECT_EQ(0, store.size()) << "REQ: SafePtr can be key";

    store[SafePtr<string>()] = 100;
    EXPECT_EQ(nullptr, SafePtr<string>().get());
    EXPECT_EQ(1, store.size()) << "REQ: key=nullptr is OK";

    store[SafePtr<string>()] = 200;
    EXPECT_EQ(1,   store.size()) << "REQ: no dup key";
    EXPECT_EQ(200, store[SafePtr<string>()]) << "REQ: dup key overwrites value";

    store[SafePtr<string>(make_safe<string>("hello"))] = 300;
    EXPECT_EQ(2, store.size()) << "REQ: diff key";
}
TEST(SafePtrTest, GOLD_voidEqual_identity_not_castPath)
{
    // == answers "same object?" (identity), not "same cast path?" (view)
    // MI first-base: MiD* == MiB1* (same addr) → same identity → ==
    // user checks lastType() explicitly when cast-path distinction is needed
    auto d = make_safe<MiD>();
    SafePtr<void> vD  = d;                // lastType=MiD, raw=MiD*
    SafePtr<void> vB1 = SafePtr<MiB1>(d); // lastType=MiB1, raw=MiB1*=MiD* (first base)
    EXPECT_VALID(vD);
    EXPECT_VALID(vB1);

    EXPECT_EQ(vD.get().get(), vB1.get().get()) << "REQ: first-base addr == MiD addr";
    EXPECT_EQ(vD, vB1) << "REQ: same ptr → same object → == (identity)";
    EXPECT_NE(vD.lastType(), vB1.lastType()) << "REQ: lastType differs — user checks explicitly";
}
TEST(SafePtrTest, GOLD_voidEqual_diffObj)
{
    // MI second-base: MiB2* ≠ MiD* → different raw addrs → !=
    // BUT same ctrl-block → std::owner_less treats them as one key (the classic owner_less use case)
    auto d = make_safe<MiD>();
    SafePtr<void> v1 = SafePtr<MiB1>(d);
    SafePtr<void> v2 = SafePtr<MiB2>(d);
    EXPECT_VALID(v1);
    EXPECT_VALID(v2);
    EXPECT_NE(v1.get().get(), v2.get().get()) << "REQ: MI second-base has diff addr";
    EXPECT_NE(v1, v2) << "REQ: diff addr → !=";

    map<SafePtr<void>, string, std::owner_less<SafePtr<void>>> ownerMap;
    ownerMap[v1] = "B1";
    ownerMap[v2] = "B2";
    EXPECT_EQ(1, ownerMap.size()) << "REQ: owner-based map collapses same ctrl-block";
    EXPECT_EQ("B2", ownerMap[v1]) << "REQ: same-owner lookup wins";
}
TEST(SafePtrTest, voidEqual_samePath)
{
    auto d = make_safe<int>(42);
    SafePtr<void> v1(d);
    SafePtr<void> v2(d);
    EXPECT_VALID(v1);
    EXPECT_VALID(v2);
    EXPECT_EQ(v1, v2) << "REQ: same ptr → ==";
    EXPECT_FALSE(v1 != v2) << "REQ: != consistent";
    EXPECT_FALSE(v1 < v2) << "REQ: < consistent with ==";
    EXPECT_FALSE(v2 < v1) << "REQ: < consistent with ==";
}
TEST(SafePtrTest, voidEqual_null)
{
    SafePtr<void> n1, n2;
    EXPECT_VALID(n1);
    EXPECT_EQ(n1, n2) << "REQ: null == null";
    EXPECT_FALSE(n1 != n2) << "REQ: != consistent";
    EXPECT_FALSE(n1 < n2) << "REQ: < consistent";
}
TEST(SafePtrTest, GOLD_crossType_compare)
{
    // ==/!=/< are template<T,U>: must compare ptr-identity across DIFFERENT SafePtr<T>
    auto d  = make_safe<Derive>();
    SafePtr<Base> b = d;              // same object, Derive-view vs Base-view (single inherit: same addr)
    SafePtr<void> v = b;

    // cross-type ==/!= : Derive vs Base vs void all alias the same object
    EXPECT_EQ(d, b)  << "REQ: SafePtr<Derive>==SafePtr<Base> same object";
    EXPECT_EQ(b, v)  << "REQ: SafePtr<Base>  ==SafePtr<void> same object";
    EXPECT_EQ(d, v)  << "REQ: SafePtr<Derive>==SafePtr<void> same object";
    EXPECT_FALSE(d != b) << "REQ: cross-type != consistent with ==";
    EXPECT_FALSE(d < v)  << "REQ: cross-type < consistent with == (same addr)";
    EXPECT_FALSE(v < d)  << "REQ: cross-type < consistent with == (same addr)";

    // cross-type vs a different object: never equal, strict-weak-order total
    auto d2 = make_safe<Derive>();
    SafePtr<Base> b2 = d2;
    EXPECT_NE(d, b2) << "REQ: cross-type != for different objects";
    EXPECT_TRUE((d < b2) ^ (b2 < d)) << "REQ: cross-type < gives strict order between diff objects";

    // cross-type vs nullptr
    SafePtr<Base> nb;
    SafePtr<void> nv;
    EXPECT_NE(d, nb) << "REQ: non-null != null (cross-type)";
    EXPECT_EQ(nb, nv) << "REQ: null==null (cross-type Base vs void)";

    // MI second-base: different sub-object addr → cross-type != (compared via void, the key type)
    auto m = make_safe<MiD>();
    SafePtr<void> vb1 = SafePtr<MiB1>(m);  // first base: same addr as MiD
    SafePtr<void> vb2 = SafePtr<MiB2>(m);  // second base: shifted addr
    EXPECT_EQ(SafePtr<void>(m), vb1) << "REQ: first-base shares addr → ==";
    EXPECT_NE(vb1, vb2) << "REQ: MI second-base diff addr → != (via void)";
}
TEST(SafePtrTest, GOLD_voidMap_sameObj_oneKey)
{
    // same object via different cast paths → same key in map (identity, not view)
    auto d = make_safe<MiD>();
    SafePtr<void> vD  = d;
    SafePtr<void> vB1 = SafePtr<MiB1>(d);
    EXPECT_VALID(vD);
    EXPECT_VALID(vB1);
    EXPECT_EQ(vD, vB1) << "precondition: same identity (first-base shares addr with D)";

    // ordered map: second insert overwrites first (same key)
    map<SafePtr<void>, string> m;
    m[vD]  = "D";
    m[vB1] = "B1";
    EXPECT_EQ(1, m.size()) << "REQ: same object → one key";
    EXPECT_EQ("B1", m[vD]) << "REQ: last write wins";

    // unordered map: same
    unordered_map<SafePtr<void>, string> um;
    um[vD]  = "D";
    um[vB1] = "B1";
    EXPECT_EQ(1, um.size()) << "REQ: same object → one key";
    EXPECT_EQ("B1", um[vD]) << "REQ: last write wins";
}

#define WEAK
// ***********************************************************************************************
// EBO: SafeWeak<T≠void> = sizeof(weak_ptr), SafeWeak<void> = weak_ptr + type_index
static_assert(sizeof(SafeWeak<int>) == sizeof(std::weak_ptr<int>), "EBO: zero overhead for non-void SafeWeak");
static_assert(sizeof(SafeWeak<void>) == sizeof(std::weak_ptr<void>) + sizeof(std::type_index), "EBO: +8B for void SafeWeak only");

TEST(SafePtrTest, GOLD_convert_SafePtr_WeakPtr)
{
    // create SafeWeak
    auto d = make_safe<Derive>();
    SafeWeak<Derive> w = d;
    EXPECT_EQ(1, d.use_count()) << "REQ: SafeWeak not inc use_count.";
    EXPECT_FALSE(w.expired()) << "REQ: SafeWeak flag.";

    // SafeWeak -> SafePtr
    auto dd = w.lock();
    EXPECT_VALID(dd);
    EXPECT_EQ(d, dd) << "REQ: same";
    EXPECT_EQ(d.lastType(), dd.lastType()) << "REQ: same";
    EXPECT_EQ(2, d.use_count()) << "REQ: SafeWeak not inc use_count.";
    EXPECT_FALSE(w.expired()) << "REQ: SafeWeak flag.";

    // del SafePtr
    d = nullptr;
    EXPECT_VALID(d);
    EXPECT_EQ(1, dd.use_count()) << "REQ: SafeWeak not inc use_count.";
    EXPECT_FALSE(w.expired()) << "REQ: SafeWeak flag.";

    dd = nullptr;
    EXPECT_VALID(dd);
    EXPECT_TRUE(w.expired()) << "REQ: SafeWeak flag.";
    EXPECT_EQ(nullptr, w.lock().get()) << "REQ: no more.";
}
TEST(SafePtrTest, safeWeak_void)
{
    auto d = make_safe<Derive>();
    SafePtr<void> dbv = SafePtr<Base>(d);  // Derive->Base->void
    EXPECT_VALID(dbv);
    SafeWeak<void> dbvw = dbv;
    EXPECT_FALSE(dbvw.expired()) << "REQ: SafeWeak<void> not expired.";
    EXPECT_EQ(type_index(typeid(Base)), dbv.lastType()) << "REQ: lastType=Base";

    // lock & verify round-trip
    auto dbv2 = dbvw.lock();
    EXPECT_VALID(dbv2);
    EXPECT_NE(nullptr, dbv2.get()) << "REQ: lock ok";
    EXPECT_EQ(type_index(typeid(Base)), dbv2.lastType()) << "REQ: lock preserves lastType";
    EXPECT_EQ(1, safe_cast<Base>(dbv2)->value()) << "REQ: round-trip void->Base ok";

    // expire
    d = nullptr;
    EXPECT_VALID(d);
    dbv = nullptr;
    EXPECT_VALID(dbv);
    dbv2 = nullptr;
    EXPECT_VALID(dbv2);
    EXPECT_TRUE(dbvw.expired()) << "REQ: SafeWeak<void> expired.";
    EXPECT_EQ(nullptr, dbvw.lock().get()) << "REQ: lock after expire -> nullptr.";
}
TEST(SafePtrTest, safeWeak_nullptr)
{
    SafeWeak<int> w = SafePtr<int>();
    EXPECT_TRUE(w.expired()) << "REQ: weak from nullptr is expired";
    EXPECT_EQ(nullptr, w.lock().get()) << "REQ: lock nullptr weak → null";

    SafeWeak<void> wv = SafePtr<void>();
    EXPECT_TRUE(wv.expired()) << "REQ: void weak from nullptr is expired";
    EXPECT_EQ(nullptr, wv.lock().get()) << "REQ: lock nullptr void weak → null";
}
TEST(SafePtrTest, safeWeak_default_classMember)
{
    // default ctor: empty weak (eg as class member to be assigned later)
    SafeWeak<Derive> w;
    EXPECT_TRUE(w.expired()) << "REQ: default-constructed weak is expired";
    EXPECT_EQ(nullptr, w.lock().get()) << "REQ: default lock → null";

    SafeWeak<void> wv;
    EXPECT_TRUE(wv.expired()) << "REQ: default void weak is expired";

    // typical use: declare uninit member, assign later
    struct Subject { SafeWeak<Derive> obs_; };
    Subject s;
    EXPECT_TRUE(s.obs_.expired()) << "REQ: default-init class member is expired";

    auto p = make_safe<Derive>();
    s.obs_ = p;  // implicit SafePtr→SafeWeak conversion + compiler-gen move-assign
    EXPECT_FALSE(s.obs_.expired()) << "REQ: member tracks ptr after assign";
    EXPECT_EQ(1, p.use_count())    << "REQ: weak does not bump strong count";
}
TEST(SafePtrTest, GOLD_safeWeak_ownerMap_observerTable)
{
    // canonical observer registry: subject keeps weak refs in a map keyed by owner identity
    // - owner_less collapses alias weaks (same ctrl-block) into one key
    // - works even after the watched object dies (raw addr would be UB; ctrl-block stays)
    auto obs1      = make_safe<Derive>();
    auto obs2      = make_safe<Derive>();
    SafeWeak<Derive> w1     = obs1;
    SafeWeak<Derive> w1Alias = obs1;  // same ctrl-block → same owner key
    SafeWeak<Derive> w2     = obs2;

    map<SafeWeak<Derive>, string, std::owner_less<SafeWeak<Derive>>> registry;
    registry[w1     ] = "obs1";
    registry[w1Alias] = "obs1-alias";  // overwrites w1's entry: same owner
    registry[w2     ] = "obs2";
    EXPECT_EQ(2, registry.size()) << "REQ: owner-based dedup (alias weaks → 1 key)";
    EXPECT_EQ("obs1-alias", registry[w1]) << "REQ: alias overwrote, same key";

    // observer dies → all alias weaks expire together; map key still valid for cleanup pass
    obs1 = nullptr;
    EXPECT_TRUE(w1.expired())      << "REQ: weak expired after observer death";
    EXPECT_TRUE(w1Alias.expired()) << "REQ: all alias weaks expire together";
    EXPECT_FALSE(w2.expired())     << "REQ: unrelated weak unaffected";
    EXPECT_EQ(1, registry.count(w1)) << "REQ: dead-key still indexable via owner identity";
}

#define EXCEPT
// ***********************************************************************************************
TEST(SafePtrTest, except_safe)
{
    struct E {
        E() { throw runtime_error("construct except"); }
    };
    EXPECT_EQ(nullptr, make_safe<E>().get()) << "REQ: make_safe except->nullptr";
    EXPECT_EQ(nullptr, new_safe<E>().get()) << "REQ: new_safe except->nullptr";
}

#define NEW_SAFE
// ***********************************************************************************************
TEST(SafePtrTest, GOLD_newSafe_basic)
{
    // Test with int - same type as make_safe tests
    auto i = new_safe<int>(42);
    EXPECT_VALID(i);
    EXPECT_EQ(1, i.use_count()) << "REQ: compatible shared_ptr";

    auto content = i.get();
    EXPECT_EQ(42, *content) << "REQ: valid get";
    EXPECT_EQ(2, i.use_count()) << "REQ: compatible shared_ptr";

    *content = 43;
    EXPECT_EQ(43, *i.get()) << "REQ: valid update";

    content = nullptr;
    EXPECT_EQ(43, *i.get()) << "REQ: outside reset not impact SafePtr";
    EXPECT_EQ(1, i.use_count()) << "REQ: compatible shared_ptr";
    EXPECT_VALID(i);

    // default construction
    auto ptr = new_safe<int>();
    EXPECT_EQ(0, *ptr.get()) << "REQ: empty new_safe like make_shared";
    EXPECT_EQ(1, ptr.use_count()) << "REQ: compatible shared_ptr";
    EXPECT_VALID(ptr);
}

struct BigObj {
    char data[4096];  // simulate big object
};
TEST(SafePtrTest, GOLD_newSafe_vs_makeSafe_memBehavior)
{
    #ifndef IN_SPECIAL_UT
    GTEST_SKIP() << "valgrind interposes operator new, so this allocation probe is not observable";
    #endif

    // - Core difference while SafeWeak is still alive:
    // - make_safe keeps the BigObj-sized co-allocation; new_safe frees BigObj immediately.
    resetBigAllocProbe(sizeof(BigObj));
    {
        SafePtr<BigObj> p_make = make_safe<BigObj>();
        EXPECT_VALID(p_make);
        EXPECT_EQ(1, g_probeAlloc);

        [[maybe_unused]] SafeWeak<BigObj> w_make = p_make;
        p_make = nullptr;  // last SafePtr dies
        EXPECT_EQ(0, g_probeFree) << "make_safe: SafeWeak still pins BigObj-sized allocation";
    }
    EXPECT_EQ(1, g_probeFree) << "make_safe: allocation freed only after SafeWeak dies";

    resetBigAllocProbe(sizeof(BigObj));
    {
        SafePtr<BigObj> p_new = new_safe<BigObj>();
        EXPECT_VALID(p_new);
        EXPECT_EQ(1, g_probeAlloc);

        [[maybe_unused]] SafeWeak<BigObj> w_new = p_new;
        p_new = nullptr;  // last SafePtr dies
        EXPECT_EQ(1, g_probeFree) << "REQ new_safe: BigObj freed while SafeWeak still exists";
    }
    stopBigAllocProbe();
}

TEST(SafePtrTest, newSafe_cast_and_weak_compatible)
{
    // new_safe should work with all cast operations like make_safe
    auto d = new_safe<Derive>();
    EXPECT_VALID(d);

    // cast to base
    SafePtr<Base> b = d;
    EXPECT_VALID(b);
    EXPECT_EQ(1, b->value()) << "REQ: new_safe supports Derive->Base";

    // cast to void and back
    SafePtr<void> v = b;
    EXPECT_VALID(v);
    auto backB = safe_cast<Base>(v);
    EXPECT_VALID(backB);
    EXPECT_EQ(1, backB->value()) << "REQ: new_safe supports void round-trip";

    // downcast
    auto backD = safe_cast<Derive>(b);
    EXPECT_VALID(backD);
    EXPECT_EQ(1, backD->value()) << "REQ: new_safe supports downcast";

    // SafeWeak compatibility
    auto d2 = new_safe<Derive>();
    SafeWeak<Derive> w = d2;
    EXPECT_EQ(1, d2.use_count()) << "REQ: SafeWeak not inc use_count";
    EXPECT_FALSE(w.expired()) << "REQ: weak valid";

    auto dd = w.lock();
    EXPECT_VALID(dd);
    EXPECT_EQ(d2, dd) << "REQ: same object";
    EXPECT_EQ(2, d2.use_count()) << "REQ: lock increments use_count";

    d2 = nullptr;
    dd = nullptr;
    EXPECT_TRUE(w.expired()) << "REQ: weak expired after all SafePtr gone";
}
TEST(SafePtrTest, newSafe_destruct_byVoid)
{
    // new_safe must correctly destruct through void pointer
    // Reuse TestBase to minimize new template instantiations
    bool isBaseOver = false;

    SafePtr<void> v = new_safe<TestBase>(isBaseOver);
    EXPECT_VALID(v);
    EXPECT_FALSE(isBaseOver) << "REQ: constructed ok";

    v = nullptr;
    EXPECT_VALID(v);
    EXPECT_TRUE(isBaseOver) << "REQ: new_safe correctly destructs through void";
}
TEST(SafePtrTest, newSafe_destruct_byBase)
{
    // new_safe must correctly destruct derived through base pointer
    bool isBaseOver = false;
    bool isDeriveOver = false;

    SafePtr<TestBase> p = new_safe<TestDerive>(isBaseOver, isDeriveOver);
    EXPECT_VALID(p);
    EXPECT_FALSE(isDeriveOver) << "REQ: constructed ok";

    p = nullptr;
    EXPECT_VALID(p);
    EXPECT_TRUE(isDeriveOver) << "REQ: new_safe correctly destructs derived through base";
}

#define SECURITY
// ***********************************************************************************************
// - vs shared_ptr vulns: type confusion, stale state, self-move, MI addr mismatch
TEST(SafePtrTest, GOLD_voidMv_lastType_consistent)
{
    // mv ctor
    SafePtr<void> v1(make_safe<int>(42));
    EXPECT_VALID(v1);
    SafePtr<void> v2(std::move(v1));
    EXPECT_VALID(v2);
    EXPECT_EQ(type_index(typeid(int)), v2.lastType()) << "REQ: target preserves lastType";
    EXPECT_EQ(42, *safe_cast<int>(v2).get()) << "REQ: target data intact";
    EXPECT_VALID(v1);
    EXPECT_EQ(nullptr, v1.get()) << "REQ: source null after move";

    // mv assign
    SafePtr<void> v3(make_safe<float>(3.14f));
    EXPECT_VALID(v3);
    SafePtr<void> v4;
    v4 = std::move(v3);
    EXPECT_VALID(v4);
    EXPECT_EQ(type_index(typeid(float)), v4.lastType()) << "REQ: target preserves lastType";
    EXPECT_VALID(v3);
    EXPECT_EQ(nullptr, v3.get()) << "REQ: source null after move-assign";
}
TEST(SafePtrTest, selfAssign_void_safe)
{
    // self-move-assign: must keep both data AND lastType (else void→T cast loses data)
    SafePtr<void> v(make_safe<int>(42));
    EXPECT_VALID(v);
    auto* pv = &v;
    *pv = std::move(v);
    EXPECT_VALID(v);  // catches lastType wiped to void via tightened invariant
    EXPECT_EQ(42, *safe_cast<int>(v).get()) << "REQ: data reachable after self-mv=";

    // self-copy-assign
    v = SafePtr<void>(make_safe<int>(42));  // reset to valid
    EXPECT_VALID(v);
    pv = &v;
    *pv = v;
    EXPECT_VALID(v);
    EXPECT_EQ(42, *safe_cast<int>(v).get()) << "REQ: data intact after self-cp=";
}

TEST(SafePtrTest, voidCpFromMovedFrom_safe)
{
    SafePtr<void> v1(make_safe<int>(42));
    SafePtr<void> v2(std::move(v1));
    EXPECT_VALID(v1);
    // v1 is moved-from; copying it must be safe
    SafePtr<void> v3(v1);
    EXPECT_VALID(v3);
    EXPECT_EQ(nullptr, v3.get()) << "REQ: cp from moved-from → null";
    EXPECT_EQ(type_index(typeid(void)), v3.lastType()) << "REQ: cp from moved-from → lastType=void";

    // void→void cp (valid source)
    SafePtr<void> v4(v2);
    EXPECT_VALID(v4);
    EXPECT_EQ(42, *safe_cast<int>(v4).get()) << "REQ: void→void cp preserves data";
    EXPECT_EQ(type_index(typeid(int)), v4.lastType()) << "REQ: void→void cp preserves lastType";
    EXPECT_EQ(v2.get(), v4.get()) << "REQ: shared ownership";
}
TEST(SafePtrTest, safeCastFromMovedFrom_safe)
{
    SafePtr<void> v1(make_safe<int>(42));
    SafePtr<void> v2(std::move(v1));
    EXPECT_VALID(v1);
    // safe_cast from moved-from must not crash
    auto i = safe_cast<int>(v1);
    EXPECT_VALID(i);
    EXPECT_EQ(nullptr, i.get()) << "REQ: cast from moved-from → null";
}

TEST(SafePtrTest, GOLD_MI_D_B1_void_B2_typeConfusion)
{
    // C1: verify MiD->MiB1->void->MiB2 is BLOCKED (type confusion via MI)
    auto d = make_safe<MiD>();
    EXPECT_VALID(d);

    // MiD→MiB1→void (lastType_=MiB1, void* stores MiB1 sub-object addr)
    SafePtr<void> v = SafePtr<MiB1>(d);
    EXPECT_VALID(v);
    EXPECT_EQ(type_index(typeid(MiB1)), v.lastType()) << "REQ: lastType=MiB1";

    // void→MiB2 MUST fail: lastType=MiB1 ≠ MiB2, addr offset would be wrong
    auto wrongB2 = safe_cast<MiB2>(v);
    EXPECT_VALID(wrongB2);
    EXPECT_EQ(nullptr, wrongB2.get()) << "REQ: MiD→MiB1→void→MiB2 blocked (type confusion)";

    // void→MiB1 must succeed
    auto backB1 = safe_cast<MiB1>(v);
    EXPECT_VALID(backB1);
    ASSERT_NE(nullptr, backB1.get()) << "REQ: MiD→MiB1→void→MiB1 ok";
    EXPECT_EQ(11, backB1->b1) << "REQ: correct MiB1 value";

    // void→MiD must fail (lastType=MiB1, not MiD)
    auto wrongD = safe_cast<MiD>(v);
    EXPECT_VALID(wrongD);
    EXPECT_EQ(nullptr, wrongD.get()) << "REQ: MiD→MiB1→void→MiD blocked (lastType≠MiD)";

    // correct path: MiD→MiB1→MiD→MiB2
    auto backD = safe_cast<MiD>(SafePtr<MiB1>(d));
    EXPECT_VALID(backD);
    if (backD) {
        SafePtr<MiB2> correctB2(backD);
        EXPECT_VALID(correctB2);
        EXPECT_EQ(22, correctB2->b2) << "REQ: correct path MiD→MiB1→MiD→MiB2 ok";
    }
}

TEST(SafePtrTest, safe_virtual_MI_diamond)
{
    struct VBase { virtual ~VBase() = default; int v = 0; };
    struct Left  : virtual VBase { int left = 1; };
    struct Right : virtual VBase { int right = 2; };
    struct Diamond : Left, Right { Diamond() { v = 10; left = 11; right = 12; } };

    auto d = make_safe<Diamond>();
    EXPECT_VALID(d);

    // Diamond→Left→void→Left round-trip
    SafePtr<void> vLeft = SafePtr<Left>(d);
    EXPECT_VALID(vLeft);
    EXPECT_EQ(type_index(typeid(Left)), vLeft.lastType());
    auto backLeft = safe_cast<Left>(vLeft);
    EXPECT_VALID(backLeft);
    ASSERT_NE(nullptr, backLeft.get()) << "REQ: Diamond→Left→void→Left ok";
    EXPECT_EQ(11, backLeft->left);

    // Left→Diamond via dynamic_cast (virtual inheritance)
    auto backD1 = safe_cast<Diamond>(backLeft);
    EXPECT_VALID(backD1);
    ASSERT_NE(nullptr, backD1.get()) << "REQ: Diamond→Left→void→Left→Diamond ok";
    EXPECT_EQ(10, backD1->v);

    // Diamond→VBase→void→VBase round-trip (virtual base)
    SafePtr<void> vBase = SafePtr<VBase>(d);
    EXPECT_VALID(vBase);
    EXPECT_EQ(type_index(typeid(VBase)), vBase.lastType());
    auto backBase = safe_cast<VBase>(vBase);
    EXPECT_VALID(backBase);
    ASSERT_NE(nullptr, backBase.get()) << "REQ: Diamond→VBase→void→VBase ok";
    EXPECT_EQ(10, backBase->v);

    // VBase→Diamond via dynamic_cast (cross virtual inheritance)
    auto backD2 = safe_cast<Diamond>(backBase);
    EXPECT_VALID(backD2);
    ASSERT_NE(nullptr, backD2.get()) << "REQ: Diamond→VBase→void→VBase→Diamond ok";
    EXPECT_EQ(12, backD2->right);

    // cross-path blocked: Diamond→Left→void→Right
    auto wrongRight = safe_cast<Right>(vLeft);
    EXPECT_VALID(wrongRight);
    EXPECT_EQ(nullptr, wrongRight.get()) << "REQ: Diamond→Left→void→Right blocked";

    // cross-path blocked: Diamond→Left→void→VBase
    auto wrongBase = safe_cast<VBase>(vLeft);
    EXPECT_VALID(wrongBase);
    EXPECT_EQ(nullptr, wrongBase.get()) << "REQ: Diamond→Left→void→VBase blocked";
}

TEST(SafePtrTest, weakFromMovedFrom_safe)
{
    SafePtr<void> v(make_safe<int>(42));
    SafePtr<void> v2(std::move(v));
    EXPECT_VALID(v);
    // create weak from moved-from
    SafeWeak<void> w = v;
    EXPECT_TRUE(w.expired()) << "REQ: weak from moved-from is expired";
    EXPECT_EQ(nullptr, w.lock().get()) << "REQ: lock moved-from weak → null";
}

}  // namespace
