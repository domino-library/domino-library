/**
 * Copyright 2018 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "ObjAnywhere.hpp"

using namespace std;

namespace rlib
{
unique_ptr<DataStore<ObjName>> ObjAnywhere::name_obj_S_;

// ***********************************************************************************************
void ObjAnywhere::deinit() noexcept
{
    // diagnostic log before destroying all objects
    if (name_obj_S_ && name_obj_S_->nData() > 0)
        HID("(ObjAnywhere) deinit with nObj=" << name_obj_S_->nData());
    name_obj_S_.reset();
}

// ***********************************************************************************************
void ObjAnywhere::init(UniLog& oneLog) noexcept
{
    if (isInit())
    {
        WRN("(ObjAnywhere) !!! Refuse dup init.");
        return;
    }
    try {  // noexcept-correct: make_unique may throw bad_alloc; tolerate it (lib v9 goal)
        name_obj_S_ = make_unique<DataStore<ObjName>>();
        HID("(ObjAnywhere) Succeed.");
    } catch(...) {
        ERR("(ObjAnywhere) !!! init failed, except=" << mt_exceptInfo());
    }
}
}  // namespace
