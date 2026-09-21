/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// ISSUE: C++ memory bugs (US Gov suggests Rust; MS/Google: ~70% safety defects are mem-related)
//
// REQ/value: 100% safe smart pointer (subset shared_ptr)
// - Create:
//   . Only allow safe creation via eg make_safe<T>() or nullptr
//   . Forbid unsafe eg via raw pointer/shared_ptr construction
// - Cast (safe_cast<T>()):
//   . Only allow safe cast: eg among self, base & void, especially multi-inherit cast
//   . Unsafe cast: return nullptr, or compile-time error (better)
// - Copy/Move/Assign:
//   . Only allow safe operations eg between same type, to base/void/const
//   . Unsafe operations (eg to derive, const→non-const): return nullptr, or compile-time error (better)
// - Access, eg:
//   . safe get() returns shared_ptr<T> (not raw T*, safer lifecycle management)
//   . Correct destructor call via shared_ptr (even through void*/base*)
// - Additional Features:
//   * max (safe + convenient) / (mem + perf + impl)
//   . SafeWeak: weak reference support (lock()/expired())
//   . Container support: usable as map/unordered_map key
//   . Exception safe: construction exception → nullptr
//
// CONSTRAINTS:
//   - Single-thread-use as shared_ptr (e.g., after MtInQueue.mt_push(), don't touch SafePtr)
//   - T must be internally safe (eg no constructor exceptions expected)
//   * Circular reference: user's duty (like shared_ptr)
//   . eg force-cast SafePtr<T> to SafePtr<U>: user's duty
// ***********************************************************************************************
#pragma once

#include <cstdlib>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "UniLog.hpp"

namespace rlib
{
template<typename T> class SafeWeak;

// EBO: type_index only exists for SafePtr<void>, zero overhead for SafePtr<T≠void>
template<bool> struct void_type_index {};
template<>     struct void_type_index<true> { std::type_index lastType_ = typeid(void); };  // type before cast to void

// ***********************************************************************************************
template<typename T = void>
class SafePtr : private void_type_index<std::is_void_v<T>>
{
public:
    using element_type = T;  // smart-pointer metadata, like std::shared_ptr

    // - safe-only creation, eg can't construct by raw ptr/shared_ptr that maybe unsafe
    // - can't create by SafePtr(ConstructArgs...) that confuse cp constructor, make_safe() instead
    // - T(ConstructArgs) SHALL mem-safe
    constexpr
        SafePtr(std::nullptr_t = nullptr) noexcept {}
    template<typename U, typename... ConstructArgs> friend
        SafePtr<U> make_safe(ConstructArgs&&...) noexcept;  // for most T
    template<typename U, typename... ConstructArgs> friend
        SafePtr<U> new_safe(ConstructArgs&&...) noexcept;  // for big T, avoid SafeWeak block free T's mem
    // clarity: forbid unsafe create from raw ptr / shared_ptr
    template<typename U>
        SafePtr(U*) = delete;
    template<typename U>
        SafePtr(const std::shared_ptr<U>&) = delete;

    template<typename From> friend class SafePtr;  // let cp/mv access private
    friend class SafeWeak<T>;  // so SafeWeak ctor reads pT_ & SafeWeak.lock() builds SafePtr
    // cmp
    template<typename T1, typename U1> friend bool operator==(const SafePtr<T1>&, const SafePtr<U1>&) noexcept;
    template<typename T1, typename U1> friend bool operator< (const SafePtr<T1>&, const SafePtr<U1>&) noexcept;
    friend struct std::hash<SafePtr>;
    // safe-only cp/mv diff-type (vs shared_ptr, eg static_pointer_cast<any> is not safe)
    // - SFINAE: 1)testable 2)more clear compile-err 3)overload resolution: trim & predictable
    // - by-value: lvalue→cp param then mv pT_; rvalue→mv param then mv pT_ (extra mv negligible)
    template<typename From, std::enable_if_t<std::is_convertible_v<From*, T*>, int> = 0>
        SafePtr(SafePtr<From>) noexcept;  // cp & mv, ok (or compile err)

    // same-type mv/mv=: forbid compiler-gen - not safe
    SafePtr(SafePtr&& aSrc) noexcept;
    SafePtr& operator=(SafePtr&& aSrc) noexcept;
    // same-type cp/cp=: self mv suppress compiler-gen so explicit
    SafePtr(const SafePtr&) = default;
    SafePtr& operator=(const SafePtr&) = default;

    // safe cast
    template<typename To> [[nodiscard]]
        std::shared_ptr<To> cast() const noexcept;  // ret ok/null; safe cast
    template<typename To, typename From> friend
        SafePtr<To> safe_cast(const SafePtr<From>&) noexcept;  // ret ok/null

    // safe usage: convenient(compatible shared_ptr), equivalent & min
    // . ret shared_ptr is safer than T* (but not safest since to call T's func easily)
    // . no operator*() since T& is unsafe
    [[nodiscard]]
        std::shared_ptr<T> get() const noexcept { return pT_; }
    std::shared_ptr<T> operator->() const noexcept;  // convenient; unsafe to ret ref
    explicit operator bool() const noexcept { return pT_ != nullptr; }
    [[nodiscard]] auto use_count() const noexcept { return pT_.use_count(); }
    void swap(SafePtr& aOther) noexcept;
    template<typename U> [[nodiscard]]
        bool owner_before(const SafePtr<U>& aOther) const noexcept { return pT_.owner_before(aOther.pT_); }

    // needed by safe_cast (preserves orig type across void↔T round-trip); also for debug
    [[nodiscard]] auto lastType() const noexcept;

private:
    constexpr SafePtr(std::shared_ptr<T>&&, const std::type_index&) noexcept;

    // -------------------------------------------------------------------------------------------
    std::shared_ptr<T> pT_;  // core
    // (void_type_index::lastType_) only exists for T=void, via EBO (0B for T≠void)
};

// ***********************************************************************************************
// - safe cp/mv diff-type, otherwise compile-err (safer than nullptr, like cp/mv shared_ptr)
//   . by-value param: lvalue→cp then mv pT_; rvalue→mv then mv pT_ (extra mv negligible)
//   . same-type cp/mv use non-template overloads (higher priority), no conflict
// - void->T: compile-err, can safe_cast() if need
template<typename T>
template<typename From, std::enable_if_t<std::is_convertible_v<From*, T*>, int>>
SafePtr<T>::SafePtr(SafePtr<From> aSafeFrom) noexcept  // cp & mv
    : pT_(std::move(aSafeFrom.pT_))
{
    if constexpr(std::is_void_v<T>)
        if (pT_) this->lastType_ = aSafeFrom.lastType();
}

// ***********************************************************************************************
// - same-type mv: compiler-gen doesn't reset void's lastType_ (type_index mv==cp)
template<typename T>
SafePtr<T>::SafePtr(SafePtr&& aSrc) noexcept
    : pT_(std::move(aSrc.pT_))
{
    if constexpr(std::is_void_v<T>) {
        this->lastType_ = aSrc.lastType_;
        aSrc.lastType_ = typeid(void);
    }
}
template<typename T>
SafePtr<T>& SafePtr<T>::operator=(SafePtr&& aSrc) noexcept
{
    if (this == &aSrc) return *this;  // self-mv= would wipe lastType_ via aSrc.lastType_=void
    pT_ = std::move(aSrc.pT_);
    if constexpr(std::is_void_v<T>) {
        this->lastType_ = aSrc.lastType_;
        aSrc.lastType_ = typeid(void);
    }
    return *this;
}

// ***********************************************************************************************
// - for safe_cast; constructor is faster; private is safe
template<typename T>
constexpr SafePtr<T>::SafePtr(std::shared_ptr<T>&& aPtr, [[maybe_unused]] const std::type_index& aLast) noexcept
    : pT_(std::move(aPtr))  // mv
{
    if constexpr(std::is_void_v<T>)
        this->lastType_ = pT_ ? aLast : typeid(void);
}

// ***********************************************************************************************
template<typename T>
template<typename To>
std::shared_ptr<To> SafePtr<T>::cast() const noexcept
{
    if constexpr(std::is_convertible_v<T*, To*>)
    {
        // HID("(SafePtr) cast to base/self/void/const");  // ERR() not MT safe
        return pT_;  // private/protected inherit: is_convertible_v=false, won't reach here
    }
    else if constexpr(std::is_base_of_v<T, To>)  // else if for constexpr
    {
        if constexpr(std::is_polymorphic_v<T>)
        {
            // HID("(SafePtr) downcast: ok or nullptr");
            return std::dynamic_pointer_cast<To>(pT_);
        }
        else
        {
            static_assert(std::is_polymorphic_v<T>,
                "(SafePtr) downcast requires a polymorphic source type (at least one virtual function).");
            return nullptr;  // unreachable; keeps the rejected instantiation well-formed otherwise
        }
    }
    else if constexpr(!std::is_void_v<T>)
    {
        static_assert(std::is_same_v<T, To>, "(SafePtr) unsafe cast not allowed: unrelated nonVoid-to-nonVoid.");
        return pT_;  // unreachable, but satisfies return type
    }

    else if (this->lastType_ == typeid(To))
    {
        // HID("(SafePtr) cast void->lastType (MI safe round-trip)");
        return std::static_pointer_cast<To>(pT_);  // void stored lastType's address, static_cast restores it
    }
    HID("(SafePtr) can't cast from=void/" << typeid(T).name() << " to=" << typeid(To).name());
    return nullptr;  // non-constexpr branch: dynamic_pointer_cast<To>(pT_) may fail compile
}

// ***********************************************************************************************
template<typename T>
auto SafePtr<T>::lastType() const noexcept
{
    if constexpr(std::is_void_v<T>) return this->lastType_;
    else return std::type_index(typeid(T));
}

// ***********************************************************************************************
template<typename T>
std::shared_ptr<T> SafePtr<T>::operator->() const noexcept
{
    if (!pT_)
    {
        HID("(SafePtr ERR!) operator-> on null");  // only HID is MT safe
        std::abort();  // safer: no dirty mem; easier debug: here rather than later; simpler than terminate()
    }
    return pT_;
}

// ***********************************************************************************************
template<typename T>
void SafePtr<T>::swap(SafePtr& aOther) noexcept
{
    pT_.swap(aOther.pT_);
    if constexpr(std::is_void_v<T>)
        std::swap(this->lastType_, aOther.lastType_);
}



// ***********************************************************************************************
template<typename U, typename... ConstructArgs>
[[nodiscard]] SafePtr<U> make_safe(ConstructArgs&&... aArgs) noexcept
{
    SafePtr<U> safeU;
    try {  // bad_alloc, or except from U's constructor
        safeU.pT_ = std::make_shared<U>(std::forward<ConstructArgs>(aArgs)...);  // std::make_shared, not boost's
        // HID("new ptr=" << (void*)(safeU.pT_.get()));  // too many print; void* print addr rather than content(dangerous)
    } catch(...) {
        HID("(make_safe) except=" << mt_exceptInfo());  // only HID is MT safe
    }
    return safeU;
}
template<typename U, typename... ConstructArgs>
[[nodiscard]] SafePtr<U> new_safe(ConstructArgs&&... aArgs) noexcept
{
    SafePtr<U> safeU;
    try {  // bad_alloc, or except from U's constructor
        // shared_ptr(Y*) ctor: if ctrl-blk alloc throws, Y* is deleted (std guarantee, no leak)
        safeU.pT_ = std::shared_ptr<U>(new U(std::forward<ConstructArgs>(aArgs)...));
    } catch(...) {
        HID("(new_safe) except=" << mt_exceptInfo());  // only HID is MT safe
    }
    return safeU;
}

// ***********************************************************************************************
// - SafePtr can be key of map & unordered_map (like shared_ptr)
// - ptr-only: == means "same object" (identity), not "same cast path" (view)
//   . same ptr → same shared object (shared_ptr guarantee); lastType_ is a view attribute
//   . user checks lastType() explicitly when cast-path distinction is needed
template<typename T, typename U>
bool operator==(const SafePtr<T>& lhs, const SafePtr<U>& rhs) noexcept
{
    return lhs.pT_.get() == rhs.pT_.get();  // faster than lhs.get()..; same safe
}
template<typename T, typename U>
bool operator!=(const SafePtr<T>& lhs, const SafePtr<U>& rhs) noexcept
{
    return !(lhs == rhs);
}
template<typename T, typename U>
bool operator<(const SafePtr<T>& lhs, const SafePtr<U>& rhs) noexcept
{
    return std::less<>()(lhs.pT_.get(), rhs.pT_.get());
}
template<typename T>
void swap(SafePtr<T>& lhs, SafePtr<T>& rhs) noexcept
{
    lhs.swap(rhs);
}

// ***********************************************************************************************
// - safe cast all possible (self/base/derive/void/back, more than cp constructor)
// - explicit cast so ok or nullptr (cp/mv constructor is implicit & ok/compile-err)
// - unified-ret is predictable & simple
// - std not allow overload dynamic_pointer_cast so safe_cast & DYN_PTR_CAST
template<typename To, typename From>
[[nodiscard]] SafePtr<To> safe_cast(const SafePtr<From>& aSafeFrom) noexcept
{
    return SafePtr<To>(aSafeFrom.template cast<To>(), aSafeFrom.lastType());
}



// ***********************************************************************************************
// REQ: 100% safe; min subset of weak_ptr
// - owner_before + std::owner_less: usable as map/set key (the canonical use case for weak_ptr)
template<typename T = void>
class SafeWeak : private void_type_index<std::is_void_v<T>>
{
public:
    using element_type = T;  // smart-pointer metadata, like std::weak_ptr

    SafeWeak() noexcept = default;  // empty weak; eg as class member to be assigned later
    SafeWeak(const SafePtr<T>& aSafeFrom) noexcept;
    [[nodiscard]] SafePtr<T> lock() const noexcept;
    [[nodiscard]] bool expired() const noexcept { return pT_.expired(); }
    template<typename U> [[nodiscard]]
        bool owner_before(const SafeWeak<U>& aOther) const noexcept { return pT_.owner_before(aOther.pT_); }

    template<typename From> friend class SafeWeak;  // so cross-T owner_before sees private pT_

private:
    // -------------------------------------------------------------------------------------------
    std::weak_ptr<T> pT_;  // core
    // (void_type_index::lastType_) only exists for T=void, via EBO (0B for T≠void)
};

// ***********************************************************************************************
template<typename T>
SafeWeak<T>::SafeWeak(const SafePtr<T>& aSafeFrom) noexcept
    : pT_(aSafeFrom.pT_)
{
    if constexpr(std::is_void_v<T>)
        this->lastType_ = aSafeFrom.lastType();
}

// ***********************************************************************************************
template<typename T>
SafePtr<T> SafeWeak<T>::lock() const noexcept
{
    if constexpr(std::is_void_v<T>)
        return SafePtr<T>(pT_.lock(), this->lastType_);
    else
        return SafePtr<T>(pT_.lock(), std::type_index(typeid(T)));
}
}  // namespace



// ***********************************************************************************************
template<typename T>
struct std::hash<rlib::SafePtr<T>>
{
    size_t operator()(const rlib::SafePtr<T>& aSafePtr) const noexcept
    {
        return std::hash<T*>{}(aSafePtr.pT_.get());  // raw-ptr identity: matches operator==, no refcount churn
    }
};
// std::owner_less<> (transparent) auto-works via SafePtr/SafeWeak::owner_before.
// std::owner_less<SafePtr<T>> needs explicit specialization since primary is undefined.
template<typename T>
struct std::owner_less<rlib::SafePtr<T>>
{
    bool operator()(const rlib::SafePtr<T>& lhs, const rlib::SafePtr<T>& rhs) const noexcept
    {
        return lhs.owner_before(rhs);
    }
};
template<typename T>
struct std::owner_less<rlib::SafeWeak<T>>
{
    bool operator()(const rlib::SafeWeak<T>& lhs, const rlib::SafeWeak<T>& rhs) const noexcept
    {
        return lhs.owner_before(rhs);
    }
};

// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-01-30  CSZ       1)create
// 2024-02-13  CSZ       2)usage in dom lib
// 2024-04-17  CSZ       3)strict constructor - illegal->compile-err (while *cast() can ret null)
// 2024-05-06  CSZ       - AI-gen-code
// 2024-06-28  CSZ       - dynamic_pointer_cast ok or ret null
// 2024-10-16  CSZ       - dynamic_pointer_cast to safe_cast since std not allowed
// 2025-02-13  CSZ       4)SafeWeak
// 2025-03-24  CSZ       5)tolerate exception
// 2026-04-11  CSZ       6)fix D->B->void->B; only lastType_ for SafePtr&WeakPtr<void> via EBO
// 2026-04-26  CSZ       - new_safe: separate alloc so big T freed w/o waiting SafeWeak
// 2026-04-27  CSZ       - swap and owner-based ordering support
// 2026-04-27  CSZ       - SafeWeak: default ctor + owner_before/owner_less (usable as map key)
// 2026-04-28  CSZ       - drop redundant SafePtr→SafeWeak conversion op (SafeWeak ctor covers all)
// 2026-06-08  CSZ       - perf: ==/!=/< & hash compare raw-ptr identity (no refcount churn on map keys)
// ***********************************************************************************************
// - Q&A
//   * noexcept & constexpr (for whole lib)
//     * noexcept is for safe commitment/long-term to usr
//       . so as many as possible
//       . unless unrecover (eg bad_alloc)->terminate
//       . or eg interrupt constructing (not terminate)
//     . constexpr is for performance (so not suitable as a long-term commitment)
//   * std::function can't + noexcept (since it's a template class, not a func)
//
//   . How to solve safety issue:
//     . way#1: Rust is language-based mem ctrl (heavy)
//     . way#2: tool (dynamic eg valgrind, or static eg coverity)
//       . keep legacy code/invest
//       . but less safe than Rust
//     . way#3: eg SafePtr
//       * if each app class is safe, then whole app is safe (safer than way#2/tool)
//         * like SafePtr(app class) encapsulates unsafe shared_ptr(legacy)
//         * keep legacy code/invest
//         * inner-freedom + outer-safe
//       . more lightweight than Rust
//   . suggest:
//     . any class ensure mem-safe (like MT safe)
//
//   . must replace shared_ptr in DatDom, ObjAnywhere?
//     . SafePtr is simple
//     . freq cast void->any is dangerous
//     . so worth
//   . get()/etc ret shared_ptr<T>, safe?
//     . safer than ret T* since still under lifecycle-protect
//     . but not perfect as T* still available
//     . this is the simplest way to call T's func/member - compromise convenient & safe
//   . still worth? better than shared_ptr?
//     . not perfect, but SafePtr inc safe than shared_ptr - eg create(minor), cast(major) - so worth
//     . it's possible but rare to use T* unsafely - SafePtr has this risk to exchange convenient
//     . shared_ptr reduces mem-lifecycle-mgmt(1 major-possible-bug) - so worth
//
//   . std::any vs SafePtr
//     . SafePtr is safe shared_ptr that is lifecycle ptr, std::any is not ptr nor lifecycle mgmt
//
//   * realType_ & lastType_ shall be in shared_ptr's ctrl blk - less mem
//   * like shared_ptr's deleter to cast pT_ to original type?
//     . then can dyn cast to target type, better than realType_ & lastType_
//     . need the "caster" be same para & ret - impossible
//   . encapsulate cast related? better in shared_ptr's ctrl blk
//
//   . T not ref/ptr/const?
//   . SafeRef? or like this?
//   . how about class' this?
