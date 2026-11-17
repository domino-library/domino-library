/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the PolyForm Small Business License 1.0.0
 * SPDX-License-Identifier: PolyForm-Small-Business-1.0.0
 * https://polyformproject.org/licenses/small-business/1.0.0
 * Required Notice: Copyright 2026 Shi-Zhong Chen
 */
// ***********************************************************************************************
// Why: max safety of thread-entry-fn provided by usr
// - API same as std::thread, but safer (by white-list paras)
//   . white-list: instead of black-list - readable & extendable
// - not 100% safe(eg global var) but max safety/cost(facilitation, impl, performance, mem, etc)
// ***********************************************************************************************
#pragma once

#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <type_traits>
#include <utility>

#include "SafePtrOver.hpp"
#include "UniLog.hpp"

namespace rlib
{
// ***********************************************************************************************
// validate usr's thread-entry-fn
template<class aUsrEntryFN, class aEnable = void> struct is_entry_fn : std::false_type {};  // default = false
template<class aUsrEntryFN> struct is_entry_fn<
    aUsrEntryFN,
    std::enable_if_t<std::is_function_v<  // REQ: aUsrEntryFN can be converted to a function
        std::remove_pointer_t<decltype(+std::declval<aUsrEntryFN>())>  // REQ: can't lambda with capture - out-of-ctrl
    >>
> : std::true_type {};  // true condition

// white-list of thread-entry-fn-argument:
// unique_ptr is excluded: unique_ptr(T*) accepts a raw pointer, so its origin is not controlled.
template<class aEntryArg> struct is_entry_arg : std::bool_constant<
    std::is_arithmetic_v<aEntryArg> || std::is_enum_v<aEntryArg> ||  // digit is safe
    is_safe_ptr_over<aEntryArg>::value> {};    // SafePtrOver: sole owner, built only by make_over
template<> struct is_entry_arg<std::string> : std::true_type {};     // alter: string

// ***********************************************************************************************
class MT_SafeThEntryFN {
public:
    // like std::thread(mt_aFN, mt_aArgPack...): empty mt_aArgPack is the no-arg task
    template<class aUsrEntryFN, class... aEntryArgPack, std::enable_if_t<is_entry_fn<aUsrEntryFN>::value, int> = 0>
    MT_SafeThEntryFN(aUsrEntryFN, aEntryArgPack&&...) noexcept;

    MT_SafeThEntryFN(MT_SafeThEntryFN&&) noexcept = default;
    MT_SafeThEntryFN& operator=(MT_SafeThEntryFN&&) noexcept = default;
    MT_SafeThEntryFN& operator=(std::nullptr_t) noexcept { mt_fnWithArgs_.reset(); return *this; }

    SafePtrOver<void> operator()();
    explicit operator bool() const noexcept { return mt_fnWithArgs_ != nullptr; }

    // forbid
    MT_SafeThEntryFN() = delete;
    MT_SafeThEntryFN(std::nullptr_t) = delete;
    MT_SafeThEntryFN(const MT_SafeThEntryFN&) = delete;
    MT_SafeThEntryFN& operator=(const MT_SafeThEntryFN&) = delete;

private:
    // -------------------------------------------------------------------------------------------
    // mt_call() runs on the worker
    struct MT_FnWithArgs_Base_ {
        virtual ~MT_FnWithArgs_Base_() = default;
        virtual SafePtrOver<void> mt_call() = 0;
    };
    template<class aUsrEntryFN, class aEntryArgTup> struct MT_FnWithArgs_T_ : MT_FnWithArgs_Base_ {
        aUsrEntryFN  mt_fn_;
        aEntryArgTup mt_argTup_;

        MT_FnWithArgs_T_(aUsrEntryFN mt_aFN, aEntryArgTup mt_aArgTup)
            : mt_fn_(mt_aFN), mt_argTup_(std::move(mt_aArgTup)) {}
        SafePtrOver<void> mt_call() override {
            return std::apply(
                [&](auto&... mt_aArgPack) -> SafePtrOver<void> { return mt_fn_(std::move(mt_aArgPack)...); },
                mt_argTup_
            );
        }
    };
    template<class aEntryArg> static bool checkEntryArg_(aEntryArg&&) noexcept;
    template<class aEntryArg> static auto takeoverArg_(aEntryArg&&);

    // -------------------------------------------------------------------------------------------
    std::unique_ptr<MT_FnWithArgs_Base_> mt_fnWithArgs_;
};

// ***********************************************************************************************
template<class aUsrEntryFN, class... aEntryArgPack, std::enable_if_t<is_entry_fn<aUsrEntryFN>::value, int>>
MT_SafeThEntryFN::MT_SafeThEntryFN(aUsrEntryFN mt_aFN, aEntryArgPack&&... mt_aArgPack) noexcept
{
    static_assert(std::is_invocable_r_v<SafePtrOver<void>, decltype(+mt_aFN)&, std::decay_t<aEntryArgPack>...>,
        "(MT_SafeThEntryFN) entry fn must return SafePtrOver<R>");

    const auto fn = +mt_aFN;  // to function ptr
    if constexpr (std::is_pointer_v<aUsrEntryFN>) {
        if (!fn)
        {
            ERR("(MT_SafeThEntryFN) null entry fn");
            return;
        }
    }

    // null SafePtrOver / later empty-after-move: reject before or while taking, never share the arg
    if constexpr (sizeof...(aEntryArgPack) > 0) {
        bool canTakeover = true;
        ((canTakeover &= checkEntryArg_(std::forward<aEntryArgPack>(mt_aArgPack))), ...);
        if (!canTakeover) return;
    }

    try {
        using aEntryArgTup = std::tuple<std::decay_t<aEntryArgPack>...>;
        mt_fnWithArgs_.reset(new MT_FnWithArgs_T_<decltype(fn), aEntryArgTup>(
            fn, aEntryArgTup{takeoverArg_(std::forward<aEntryArgPack>(mt_aArgPack))...}
        ));
    } catch (...) {  // GCOVR_EXCL_START alloc or copy throw inside takeoverArg_()
        EMG("(MT_SafeThEntryFN) construction except=%s", mt_exceptInfo());
    }  // GCOVR_EXCL_STOP
}

// ***********************************************************************************************
template<class aEntryArg>
bool MT_SafeThEntryFN::checkEntryArg_(aEntryArg&& aArg) noexcept
{
    using Arg = std::decay_t<aEntryArg>;
    if constexpr (std::is_same_v<Arg, char*> || std::is_same_v<Arg, const char*>) {
        static_assert(is_entry_arg<Arg>::value, "(MT_SafeThEntryFN) unsafe aEntryArg, use std::string");
    } else {
        static_assert(is_entry_arg<Arg>::value,
            "(MT_SafeThEntryFN) unsafe aEntryArg; or impl is_entry_arg<aEntryArg> to declare safe");
    }
    static_assert(std::is_rvalue_reference_v<aEntryArg&&> || std::is_copy_constructible_v<Arg>,
        "(MT_SafeThEntryFN) aEntryArg must be std::move'd");

    // SafePtrOver: null or a shared_ptr/weak still tied to this control block refuses before any move.
    // A second look at the same object still sees it here; takeoverArg_() calls detachWeak, and throws
    // once the first detachWeak has emptied it.
    if constexpr (is_safe_ptr_over<Arg>::value) {
        static_assert(std::is_rvalue_reference_v<aEntryArg&&>, "(MT_SafeThEntryFN) SafePtrOver arg must be rvalue");
        if (!aArg)
        {
            ERR("(MT_SafeThEntryFN) null SafePtrOver");
            return false;
        }
        if (!aArg.canDetachWeak())
        {
            ERR("(MT_SafeThEntryFN) SafePtrOver detachWeak failed, use_count=" << aArg.use_count());
            return false;
        }
        return true;
    }
    else return true;
}

// ***********************************************************************************************
template<class aEntryArg>
auto MT_SafeThEntryFN::takeoverArg_(aEntryArg&& aArg)
{
    // SafePtrOver. detachWeak() fails when the same object was taken twice: throw, and drop what was taken.
    if constexpr (is_safe_ptr_over<std::decay_t<aEntryArg>>::value) {
        auto taken = aArg.detachWeak();
        if (!taken) throw std::bad_alloc{};
        return taken;
    }
    // other types
    else return std::decay_t<aEntryArg>(std::forward<aEntryArg>(aArg));
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2026-11-06  CSZ       1)created
// ***********************************************************************************************
