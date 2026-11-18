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
// - white & black list:
//   . base on common sense (but/so not 100%)
//   . eg forbid struct (since hard to check all fields/sub-structs; but can pass if usr guarantees by is_entry_arg)
//   . future can inc/dec the list (to fit for real world)
// ***********************************************************************************************
#pragma once

#include <any>
#include <array>
#include <deque>
#include <forward_list>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "SafePtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<typename T> struct is_safe_ptr_type : std::false_type {};
template<typename T> struct is_safe_ptr_type<SafePtr<T>> : std::true_type {};

template<typename T> struct is_safe_weak_type : std::false_type {};
template<typename T> struct is_safe_weak_type<SafeWeak<T>> : std::true_type {};

template<typename T> struct is_std_shared_type : std::false_type {};
template<typename T> struct is_std_shared_type<std::shared_ptr<T>> : std::true_type {};

template<typename T> struct is_std_weak_type : std::false_type {};
template<typename T> struct is_std_weak_type<std::weak_ptr<T>> : std::true_type {};

template<typename T> struct is_std_function_type : std::false_type {};
template<typename T> struct is_std_function_type<std::function<T>> : std::true_type {};

template<typename T> struct is_ref_wrapper_type : std::false_type {};
template<typename T> struct is_ref_wrapper_type<std::reference_wrapper<T>> : std::true_type {};

template<typename T> struct is_std_unique_type : std::false_type {};
template<typename T, typename D> struct is_std_unique_type<std::unique_ptr<T, D>> : std::true_type {};

template<typename T> struct is_std_any : std::false_type {};
template<> struct is_std_any<std::any> : std::true_type {};

// The other end (promise, or the single future) stays behind and still touches this state.
template<typename T> struct is_shared_state_type: std::false_type {};
template<typename T> struct is_shared_state_type<std::future<T>>: std::true_type {};
template<typename T> struct is_shared_state_type<std::shared_future<T>>: std::true_type {};
template<typename T> struct is_shared_state_type<std::promise<T>>: std::true_type {};

template<typename T> struct is_string_view : std::false_type {};
template<typename C, typename Tr> struct is_string_view<std::basic_string_view<C, Tr>> : std::true_type {};

template<typename T> struct is_std_string : std::false_type {};
template<typename C, typename Tr, typename A> struct is_std_string<std::basic_string<C, Tr, A>> : std::true_type {};

// detachWeak() builds a 2nd ctrl blk; weak_this stays on the 1st, so shared_from_this() breaks
template<typename T, typename = void> struct has_from_this : std::false_type {};
template<typename T> struct has_from_this<T, std::void_t<decltype(std::declval<const T&>().weak_from_this())>>
    : std::true_type {};
template<typename T> constexpr bool has_from_this_v = has_from_this<T>::value;

template<typename T> class SafePtrOver;
template<typename T> struct is_safe_ptr_over : std::false_type {};
template<typename T> struct is_safe_ptr_over<SafePtrOver<T>> : std::true_type {};

// value stays false for a banned T, so a caller can ask without a compile error.
// check() is what make_over calls: each ban has its own static_assert.
// Aggregates are not opened: C++17 has no field list. Containers below are.
// cv is stripped before those specializations match, so const vector<int*> is still opened.
// Each nested check goes back through canConstructSafePtrOver, which strips again.
template<typename T> struct canConstructSafePtrOver;

template<typename T>
struct canConstructSafePtrOver_impl {
    using U = T;  // already cv-unqualified, except a reference keeps the cv of what it refers to
    static constexpr bool pointer        = std::is_pointer_v<U>;
    static constexpr bool member_pointer = std::is_member_pointer_v<U>;
    static constexpr bool safe_ptr       = is_safe_ptr_type<U>::value;
    static constexpr bool safe_weak      = is_safe_weak_type<U>::value;
    static constexpr bool shared         = is_std_shared_type<U>::value;
    static constexpr bool weak           = is_std_weak_type<U>::value;
    static constexpr bool array          = std::is_array_v<U>;
    static constexpr bool reference      = std::is_reference_v<T>;
    static constexpr bool ref_wrapper    = is_ref_wrapper_type<U>::value;
    static constexpr bool is_void        = std::is_void_v<T>;
    static constexpr bool function       = std::is_function_v<U>;
    static constexpr bool std_function   = is_std_function_type<U>::value;
    static constexpr bool unique         = is_std_unique_type<U>::value;
    static constexpr bool nested         = is_safe_ptr_over<U>::value;
    static constexpr bool from_this      = has_from_this_v<U>;
    static constexpr bool string_view    = is_string_view<U>::value;
    static constexpr bool std_any        = is_std_any<U>::value;
    static constexpr bool shared_state   = is_shared_state_type<U>::value;

    static constexpr bool value =
        !pointer && !member_pointer && !safe_ptr && !safe_weak && !shared && !weak && !array
        && !reference && !ref_wrapper && !is_void && !function
        && !std_function && !unique && !nested && !from_this && !string_view
        && !std_any && !shared_state;

    static constexpr void check() noexcept {
        static_assert(!pointer,        "(SafePtrOver) unsafe T pointing to other object");
        static_assert(!member_pointer, "(SafePtrOver) unsafe T is a member pointer to other object");
        static_assert(!safe_ptr,       "(SafePtrOver) unsafe T is a SafePtr type");
        static_assert(!safe_weak,      "(SafePtrOver) unsafe T is a SafeWeak type");
        static_assert(!shared,         "(SafePtrOver) unsafe T is a std::shared_ptr type");
        static_assert(!weak,           "(SafePtrOver) unsafe T is a std::weak_ptr type");
        static_assert(!array,          "(SafePtrOver) unsafe T is an array(=ptr)");
        static_assert(!reference,      "(SafePtrOver) unsafe T referring to other object");
        static_assert(!ref_wrapper,    "(SafePtrOver) unsafe T is a std::reference_wrapper type");
        static_assert(!is_void,        "(SafePtrOver) can't construct SafePtrOver for void");
        static_assert(!function,       "(SafePtrOver) can't construct SafePtrOver for function");
        static_assert(!std_function,   "(SafePtrOver) unsafe T is a std::function (eg lambda with capture)");
        static_assert(!unique,         "(SafePtrOver) unique_ptr(T*) can smuggle a raw pointer");
        static_assert(!nested,         "(SafePtrOver) nested SafePtrOver hides another owner");
        static_assert(!from_this,      "(SafePtrOver) weak_this stays on the control block detachWeak drops");
        static_assert(!string_view,    "(SafePtrOver) string_view is a pointer plus a length");
        static_assert(!std_any,        "(SafePtrOver) std::any can hold a pointer");
        static_assert(!shared_state,   "(SafePtrOver) future/promise keeps shared state on the other side");
    }
};

template<typename T>
struct canConstructSafePtrOver : canConstructSafePtrOver_impl<std::remove_cv_t<T>> {};

// A constructor arg must not smuggle a pointer or a view into the object.
// const char* / char[N] is allowed only when the object is a std::string, which copies the chars.
// Everything else reuses canConstructSafePtrOver, so a vector<int*> is refused too.
template<typename Obj, typename Arg>
struct canPassToMakeOver {
    using A = std::remove_cv_t<std::remove_reference_t<Arg>>;
    using P = std::remove_cv_t<std::conditional_t<std::is_pointer_v<A>,
        std::remove_pointer_t<A>, std::remove_all_extents_t<A>>>;
    static constexpr bool charForStr = is_std_string<std::remove_cv_t<Obj>>::value
        && std::is_same_v<P, char> && (std::is_pointer_v<A> || std::is_array_v<A>);
    static constexpr bool value = charForStr || canConstructSafePtrOver<A>::value;

    static constexpr void check() noexcept {
        if constexpr (!charForStr) canConstructSafePtrOver<A>::check();
    }
};

// ***********************************************************************************************
template<typename T, typename A>
struct canConstructSafePtrOver_impl<std::vector<T, A>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, typename A>
struct canConstructSafePtrOver_impl<std::deque<T, A>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, std::size_t N>
struct canConstructSafePtrOver_impl<std::array<T, N>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, typename U>
struct canConstructSafePtrOver_impl<std::pair<T, U>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value && canConstructSafePtrOver<U>::value;
    static constexpr void check() noexcept {
        canConstructSafePtrOver<T>::check();
        canConstructSafePtrOver<U>::check();
    }
};
template<typename... Ts>
struct canConstructSafePtrOver_impl<std::tuple<Ts...>> {
    static constexpr bool value = (canConstructSafePtrOver<Ts>::value && ...);
    static constexpr void check() noexcept { (canConstructSafePtrOver<Ts>::check(), ...); }
};
template<typename T>
struct canConstructSafePtrOver_impl<std::optional<T>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename... Ts>
struct canConstructSafePtrOver_impl<std::variant<Ts...>> {
    static constexpr bool value = (canConstructSafePtrOver<Ts>::value && ...);
    static constexpr void check() noexcept { (canConstructSafePtrOver<Ts>::check(), ...); }
};
template<typename K, typename V, typename C, typename A>
struct canConstructSafePtrOver_impl<std::map<K, V, C, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value && canConstructSafePtrOver<V>::value;
    static constexpr void check() noexcept {
        canConstructSafePtrOver<K>::check();
        canConstructSafePtrOver<V>::check();
    }
};
template<typename K, typename V, typename H, typename P, typename A>
struct canConstructSafePtrOver_impl<std::unordered_map<K, V, H, P, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value && canConstructSafePtrOver<V>::value;
    static constexpr void check() noexcept {
        canConstructSafePtrOver<K>::check();
        canConstructSafePtrOver<V>::check();
    }
};
template<typename K, typename C, typename A>
struct canConstructSafePtrOver_impl<std::set<K, C, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<K>::check(); }
};
template<typename K, typename H, typename P, typename A>
struct canConstructSafePtrOver_impl<std::unordered_set<K, H, P, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<K>::check(); }
};
template<typename T, typename A>
struct canConstructSafePtrOver_impl<std::list<T, A>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, typename A>
struct canConstructSafePtrOver_impl<std::forward_list<T, A>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename K, typename V, typename C, typename A>
struct canConstructSafePtrOver_impl<std::multimap<K, V, C, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value && canConstructSafePtrOver<V>::value;
    static constexpr void check() noexcept {
        canConstructSafePtrOver<K>::check();
        canConstructSafePtrOver<V>::check();
    }
};
template<typename K, typename C, typename A>
struct canConstructSafePtrOver_impl<std::multiset<K, C, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<K>::check(); }
};
template<typename K, typename V, typename H, typename P, typename A>
struct canConstructSafePtrOver_impl<std::unordered_multimap<K, V, H, P, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value && canConstructSafePtrOver<V>::value;
    static constexpr void check() noexcept {
        canConstructSafePtrOver<K>::check();
        canConstructSafePtrOver<V>::check();
    }
};
template<typename K, typename H, typename P, typename A>
struct canConstructSafePtrOver_impl<std::unordered_multiset<K, H, P, A>> {
    static constexpr bool value = canConstructSafePtrOver<K>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<K>::check(); }
};
// Container::value_type is T, so the element check covers the underlying container.
template<typename T, typename C>
struct canConstructSafePtrOver_impl<std::queue<T, C>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, typename C>
struct canConstructSafePtrOver_impl<std::stack<T, C>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};
template<typename T, typename C, typename Comp>
struct canConstructSafePtrOver_impl<std::priority_queue<T, C, Comp>> {
    static constexpr bool value = canConstructSafePtrOver<T>::value;
    static constexpr void check() noexcept { canConstructSafePtrOver<T>::check(); }
};

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
    return ptr_.use_count() == 1 && std::get_deleter<SafeDeleter>(ptr_.pT_);
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
    (canPassToMakeOver<U, ConstructArgs>::check(), ...);
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
