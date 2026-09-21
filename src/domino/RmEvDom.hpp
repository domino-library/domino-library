/**
 * Copyright 2023 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - why/VALUE:
//   * rm Ev and all its resource, so Dom can run very long time
//   * REQ:
//     . as simple as possible
//     . de-couple as much as possible
//     * rm completely so safe for future (eg rm-then-callback, ret ev/en then rm)
//
// - how:
//   . each dom can rm its own resource independently as public interface (de-couple)
//   . newEvent() has to reuse rm-ed Ev as early as possible - to avoid alloc fail
//   . standalone class to min impact other dom
//   . how about orphan ev after rm its parent?
//     . let user take care his rm rather than complex dom
//
// - RISK:
//   . rm ev may break links (duty-bound of user than dom classes)
//   . rm ev impact/complex dom
//   . so use RmEvDom only when necessary
//
// - class safe: yes
//   . each Dom can't fail rmEv_()
//   . no use-up mem (too many removed events; impossible in most cases)
// ***********************************************************************************************
#pragma once

#include <unordered_set>

namespace rlib
{
// ***********************************************************************************************
template<typename aDominoType>
class RmEvDom : public aDominoType
{
public:
    explicit RmEvDom(const LogName& aUniLogName = ULN_DEFAULT) : aDominoType(aUniLogName) {}

    [[nodiscard]] bool rmEvOK(const Domino::EvName& aEN) noexcept;
    [[nodiscard]] bool isRemoved(Domino::Event aEv) const noexcept override { return aDominoType::isRemoved(aEv) || isRemovedEv_.count(aEv); }
protected:
    void rmEv_(Domino::Event aValidEv) noexcept override;
    Domino::Event recycleEv_() noexcept override;

private:
    // -------------------------------------------------------------------------------------------
    // - REQ: min mem (so better than vector<bool> when almost empty even with many events)
    // - REQ: fast (eg isRemoved(), insert, del)
    // - req: better FIFO (but current unordered_set is not; acceptable)
    std::unordered_set<Domino::Event> isRemovedEv_;

public:
    using aDominoType::oneLog;
};

// ***********************************************************************************************
template<typename aDominoType>
Domino::Event RmEvDom<aDominoType>::recycleEv_() noexcept
{
    if (isRemovedEv_.empty())
        return Domino::D_EVENT_FAILED_RET;

    const auto it = isRemovedEv_.begin();
    const auto ev = *it;
    isRemovedEv_.erase(it);  // erase by iterator to avoid second lookup
    return ev;
}

// ***********************************************************************************************
template<typename aDominoType>
void RmEvDom<aDominoType>::rmEv_(Domino::Event aValidEv) noexcept
{
    aDominoType::rmEv_(aValidEv);
    isRemovedEv_.insert(aValidEv);
}

// ***********************************************************************************************
template<typename aDominoType>
bool RmEvDom<aDominoType>::rmEvOK(const Domino::EvName& aEN) noexcept
{
    const auto validEv = this->getEventBy(aEN);
    if (validEv == Domino::D_EVENT_FAILED_RET)  // invalid; most beginning check, avoid useless exe
        return false;
    // isRemoved(validEv) is always false here

    rmEv_(validEv);
    return true;
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2023-11-14  CSZ       1)create
// 2023-11-22  CSZ       - better isRemovedEv_
// 2023-11-24  CSZ       - rmEvOK->rmEv_ since ev para (EN can outer use)
// 2025-04-05  CSZ       2)tolerate exception
// ***********************************************************************************************
