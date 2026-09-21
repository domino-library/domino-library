/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - why:
//   . support both SafePtr & shared_ptr as UniPtr
//   . switch between SafePtr & shared_ptr by modifying just 1 macro in this file
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

#include <memory>  // make_shared

#if 0
namespace rlib
{
// std::~, or ambiguous with boost::~
using   UniPtr =         std::shared_ptr<void>;
#define MAKE_PTR         std::make_shared
#define S_PTR            std::shared_ptr
#define W_PTR            std::weak_ptr
#define DYN_PTR_CAST     std::dynamic_pointer_cast
#define STATIC_PTR_CAST  std::static_pointer_cast

#else
#include "SafePtr.hpp"
namespace rlib
{
using   UniPtr =         SafePtr<void>;
#define MAKE_PTR         make_safe
#define S_PTR            SafePtr
#define W_PTR            SafeWeak
#define DYN_PTR_CAST     rlib::safe_cast
#define STATIC_PTR_CAST  rlib::safe_cast
#endif

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-02-13  CSZ       1)create
// 2025-02-13  CSZ       2)all domino lib support both SafePtr & shared_ptr
// ***********************************************************************************************
