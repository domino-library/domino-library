/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the PolyForm Small Business License 1.0.0
 * SPDX-License-Identifier: PolyForm-Small-Business-1.0.0
 * https://polyformproject.org/licenses/small-business/1.0.0
 * Required Notice: Copyright 2026 Shi-Zhong Chen
 */
// ***********************************************************************************************
// REQ:
// * data safety(eg detach from weak_ptr) cross thread(eg MtQ/entry-fn/back-fn)
//   . SafePtr: focus(min) on single-thread usage; if cross-thread, use SafePtrOver instead
//   . SafePtrOver: create new to cross-thread if can detach from weak_ptr/.., otherwise create null obj
// - make_over<T> is the user's PROMISE - the whole T is cross-thread safe
//   . OBVIOUSLY to usr, T=(int*, vector<int*>, shared_ptr, SafePtr, ...) is UNSAFE
//   . banned opaque: enable_shared_from_this, because detachWeak() drops the control block weak_this still uses
//   . banned opaque: initializer_list / string_view, a copy of a pointer plus a length
//     span / mdspan are the same kind; add them when the promised standard has them
// ***********************************************************************************************
#pragma once

#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <utility>

#include "SafePtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<typename T> class SafePtrOver;
template<typename T> struct is_safe_ptr_over : std::false_type {};
template<typename T> struct is_safe_ptr_over<SafePtrOver<T>> : std::true_type {};

template<typename T> struct is_initializer_list : std::false_type {};
template<typename E> struct is_initializer_list<std::initializer_list<E>> : std::true_type {};

template<typename T> struct is_string_view : std::false_type {};
template<typename C, typename Tr> struct is_string_view<std::basic_string_view<C, Tr>> : std::true_type {};

// detachWeak() builds a 2nd ctrl blk; weak_this stays on the 1st, so shared_from_this() breaks
template<typename T, typename = void> struct has_from_this : std::false_type {};
template<typename T> struct has_from_this<T, std::void_t<decltype(std::declval<const T&>().weak_from_this())>>
    : std::true_type {};

// value stays false for a banned T, so a caller can ask without a compile error.
// check() is what make_over calls. cv is stripped first, so const string_view is still banned.
template<typename T> struct canConstructSafePtrOver;

template<typename T>
struct canConstructSafePtrOver_impl {
    using U = T;  // already cv-unqualified, except a reference keeps the cv of what it refers to
    static constexpr bool from_this   = has_from_this<U>::value;
    static constexpr bool string_view = is_string_view<U>::value;
    static constexpr bool init_list   = is_initializer_list<U>::value;

    static constexpr bool value = !from_this && !string_view && !init_list;

    static constexpr void check() noexcept {
        static_assert(!from_this,   "(SafePtrOver) weak_this stays on the control block detachWeak drops");
        static_assert(!string_view, "(SafePtrOver) string_view is a pointer plus a length");
        static_assert(!init_list,   "(SafePtrOver) initializer_list is a pointer plus a length");
    }
};

template<typename T>
struct canConstructSafePtrOver : canConstructSafePtrOver_impl<std::remove_cv_t<T>> {};


// ***********************************************************************************************
template<typename T = void>
class SafePtrOver
{
    // friend:
    template<typename U> friend class SafePtrOver;
    template<typename U, typename... ConstructArgs> friend
        SafePtrOver<U> make_over(ConstructArgs&&...) noexcept;
    template<typename To, typename From> friend
        SafePtrOver<To> safe_cast(SafePtrOver<From>&&) noexcept;
    template<typename To, typename From> friend
        SafePtr<To> safe_cast(const SafePtrOver<From>&) noexcept;

public:
    // create:
    constexpr SafePtrOver(std::nullptr_t = nullptr) noexcept : ptr_() {}

    // forbid copy:
    SafePtrOver(const SafePtrOver&) = delete;
    SafePtrOver& operator=(const SafePtrOver&) = delete;

    // move:
    SafePtrOver(SafePtrOver&&) noexcept = default;
    SafePtrOver& operator=(SafePtrOver&&) noexcept = default;
    SafePtrOver& operator=(std::nullptr_t) noexcept { ptr_ = nullptr; return *this; }
    template<typename From, std::enable_if_t<std::is_convertible_v<From*, T*> && !std::is_same_v<From, T>, int> = 0>
        SafePtrOver(SafePtrOver<From>&& aFrom) noexcept : ptr_(std::move(aFrom.ptr_)) {}

    // to SafePtr:
    // - implicit lvalue: a new SafePtr, *this kept
    template<typename U, std::enable_if_t<std::is_convertible_v<T*, U*>, int> = 0>
        operator SafePtr<U>() const & noexcept { return ptr_; }
    // - implicit rvalue: hand off, *this empty
    template<typename U, std::enable_if_t<std::is_convertible_v<T*, U*>, int> = 0>
        operator SafePtr<U>() && noexcept { return std::move(ptr_); }
    // - explicit to other type:
    template<typename To> [[nodiscard]]
        auto safe_cast() const noexcept { return ptr_.template safe_cast<To>(); }

    // compatible shared_ptr:
    [[nodiscard]] auto get() const noexcept { return ptr_.get(); }
    auto operator->() const noexcept { return ptr_.operator->(); }
    explicit operator bool() const noexcept { return bool(ptr_); }
    [[nodiscard]] auto use_count() const noexcept { return ptr_.use_count(); }

    // misc:
    [[nodiscard]] auto lastType() const noexcept { return ptr_.lastType(); }

    // core: safe to cross-thread
    [[nodiscard]] bool canDetachWeak() const noexcept;
    [[nodiscard]] SafePtrOver detachWeak() noexcept;

private:
    SafePtrOver(std::shared_ptr<T>&& aPtr, const std::type_index& aLast) noexcept
        : ptr_(std::move(aPtr), aLast) {}

    // compare via shared_ptr's ==; no shared_ptr copy, so no refcount traffic
    template<typename U>
        friend bool operator==(const SafePtrOver& aLhs, const SafePtrOver<U>& aRhs) noexcept
        { return aLhs.ptr_.get_() == aRhs.ptr_.get_(); }
    template<typename U>
        friend bool operator==(const SafePtrOver& aLhs, const SafePtr<U>& aRhs) noexcept
        { return aLhs.ptr_.get_() == aRhs.get_(); }
    template<typename U>
        friend bool operator==(const SafePtr<U>& aLhs, const SafePtrOver& aRhs) noexcept
        { return aLhs.get_() == aRhs.ptr_.get_(); }
    template<typename U>
        friend bool operator!=(const SafePtrOver& aLhs, const SafePtrOver<U>& aRhs) noexcept
        { return !(aLhs == aRhs); }
    template<typename U>
        friend bool operator!=(const SafePtrOver& aLhs, const SafePtr<U>& aRhs) noexcept
        { return !(aLhs == aRhs); }
    template<typename U>
        friend bool operator!=(const SafePtr<U>& aLhs, const SafePtrOver& aRhs) noexcept
        { return !(aLhs == aRhs); }

    // -------------------------------------------------------------------------------------------
    SafePtr<T> ptr_;
};

// ***********************************************************************************************
template<typename T>
bool SafePtrOver<T>::canDetachWeak() const noexcept
{
    // A null owner has no SafeDeleter. Check that before use_count, or the null case never reaches it.
    const auto* del = std::get_deleter<SafeDeleter>(ptr_.pT_);
    return del != nullptr && ptr_.use_count() == 1;
}

// ***********************************************************************************************
// sole make_over -> new control block, old weak_ptr expire. else nullptr, *this kept.
template<typename T>
SafePtrOver<T> SafePtrOver<T>::detachWeak() noexcept
{
    if (!canDetachWeak())
        return nullptr;
    auto* oldDel = std::get_deleter<SafeDeleter>(ptr_.pT_);
    SafePtrOver fresh;
    try {
        fresh = SafePtrOver(std::shared_ptr<T>(ptr_.pT_.get(), SafeDeleter{nullptr, oldDel->obj_}), ptr_.lastType());
    } catch (...) {
        EMG("(SafePtrOver) detachWeak() except=%s", mt_exceptInfo());
        return nullptr;  // del_ is null, so a thrown control block does not delete the object
    }
    std::get_deleter<SafeDeleter>(fresh.ptr_.pT_)->del_ = oldDel->del_;
    oldDel->del_ = nullptr;
    *this = nullptr;
    return fresh;
}

// ***********************************************************************************************
template<typename U, typename... ConstructArgs>
[[nodiscard]] SafePtrOver<U> make_over(ConstructArgs&&... aArgs) noexcept
{
    canConstructSafePtrOver<U>::check();
    try {
        U* p = new U(std::forward<ConstructArgs>(aArgs)...);
        return SafePtrOver<U>(std::shared_ptr<U>(p, SafeDeleter{
            [](void* o) { std::default_delete<U>()(static_cast<U*>(o)); },
            const_cast<std::remove_const_t<U>*>(p)}),
            std::type_index(typeid(U)));
    } catch (...) {
        EMG("make_over except=%s", mt_exceptInfo());
        return {};
    }
}

// ***********************************************************************************************
// - rvalue: move into SafePtrOver<To> and clear aFrom. failure leaves aFrom.
template<typename To, typename From>
SafePtrOver<To> safe_cast(SafePtrOver<From>&& aFrom) noexcept
{
    auto casted = aFrom.template safe_cast<To>();
    if (!casted)
        return nullptr;
    SafePtrOver<To> out(std::move(casted), aFrom.lastType());
    aFrom = nullptr;
    return out;
}
// lvalue: SafePtr. Template argument deduction does not apply the conversion operator.
template<typename To, typename From>
SafePtr<To> safe_cast(const SafePtrOver<From>& aFrom) noexcept
{
    return safe_cast<To>(SafePtr<From>(aFrom));
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2026-11-10  CSZ       1)create: move-only cross-thread owner over SafePtr
// ***********************************************************************************************
