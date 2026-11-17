/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the PolyForm Small Business License 1.0.0
 * SPDX-License-Identifier: PolyForm-Small-Business-1.0.0
 * https://polyformproject.org/licenses/small-business/1.0.0
 * Required Notice: Copyright 2026 Shi-Zhong Chen
 */
#include "MT_SafeThEntryFN.hpp"

namespace rlib
{
// ***********************************************************************************************
SafePtrOver<void> MT_SafeThEntryFN::operator()()
{
    auto mt_fnWithArgs = std::move(mt_fnWithArgs_);  // one-shot: move the fn out, so a second call ret empty
    if (!mt_fnWithArgs)
        return {};
    auto ret = mt_fnWithArgs->mt_call();
    mt_fnWithArgs.reset();  // drop entry args before the return's detachWeak sees their owners
    if (!ret)
        return {};
    auto fresh = ret.detachWeak();
    if (!fresh)
        EMG("(MT_SafeThEntryFN) entry ret detachWeak failed, use_count=%ld", static_cast<long>(ret.use_count()));
    return fresh;
}

}  // namespace
