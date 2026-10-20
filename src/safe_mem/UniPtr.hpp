/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - why:
//   . support both SafePtr & shared_ptr as UniPtr
//   . switch between SafePtr & shared_ptr by the #if in this file
//   . SPtr/WPtr: alias templates; make_ptr/static_ptr_cast/dyn_ptr_cast: function templates
//   . domino lib is an example on how to use UniPtr & switch SafePtr/shared_ptr
//
// - why supports shared_ptr?
//   . from mem-safe pov, SafePtr is the only choice
//   * but shared_ptr is more common, easier than imposing to SafePtr
//
// - principle to use SafePtr
//   . pub interface: shall use SafePtr for safety
//   . inner can directly use shared_ptr if safe: simpler & faster
// ***********************************************************************************************
#pragma once

#include <utility>

// ***********************************************************************************************
#if 0
#include <memory>
namespace rlib
{
// std::~, or ambiguous with boost::~
                     using UniPtr = std::shared_ptr<void>;
template<typename T> using   SPtr = std::shared_ptr<T>;
template<typename T> using   WPtr = std::weak_ptr<T>;

template<typename T, typename... ConstructArgs>
[[nodiscard]] SPtr<T> make_ptr(ConstructArgs&&... aArgs)
{
    return std::make_shared<T>(std::forward<ConstructArgs>(aArgs)...);
}
template<typename To, typename From>
[[nodiscard]] SPtr<To> static_ptr_cast(const SPtr<From>& aFrom) noexcept
{
    return std::static_pointer_cast<To>(aFrom);
}
template<typename To, typename From>
[[nodiscard]] SPtr<To> dyn_ptr_cast(const SPtr<From>& aFrom) noexcept
{
    return std::dynamic_pointer_cast<To>(aFrom);
}

// ***********************************************************************************************
#else
#include "SafePtr.hpp"
namespace rlib
{
                     using UniPtr = SafePtr<void>;
template<typename T> using   SPtr = SafePtr<T>;
template<typename T> using   WPtr = SafeWeak<T>;

template<typename T, typename... ConstructArgs>
[[nodiscard]] SPtr<T> make_ptr(ConstructArgs&&... aArgs) noexcept
{
    return make_safe<T>(std::forward<ConstructArgs>(aArgs)...);
}
template<typename To, typename From>
[[nodiscard]] SPtr<To> static_ptr_cast(const SPtr<From>& aFrom) noexcept
{
    return safe_cast<To>(aFrom);
}
template<typename To, typename From>
[[nodiscard]] SPtr<To> dyn_ptr_cast(const SPtr<From>& aFrom) noexcept
{
    return safe_cast<To>(aFrom);
}
#endif

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-02-13  CSZ       1)create
// 2025-02-13  CSZ       2)all domino lib support both SafePtr & shared_ptr
// 2026-10-20  CSZ       - replace macro
// ***********************************************************************************************
