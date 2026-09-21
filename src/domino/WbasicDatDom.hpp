/**
 * Copyright 2021 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - what:     simple/easy write-protect for DataDomino
// - why:      eg Yang RW para need write-protect
// - core:     wrCtrl_
// - mem-safe: true (when use SafePtr instead of shared_ptr)
// ***********************************************************************************************
#pragma once

#include <vector>

#include "UniLog.hpp"
#include "UniPtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<typename aDominoType>
class WbasicDatDom : public aDominoType
{
public:
    explicit WbasicDatDom(const LogName& aUniLogName = ULN_DEFAULT) : aDominoType(aUniLogName) {}

    [[nodiscard]] bool isWrCtrl(const Domino::EvName&) const noexcept;
    [[nodiscard]] bool wrCtrlOk(const Domino::EvName&, const bool aNewState = true) noexcept;

    [[nodiscard]] S_PTR<void> getData(const Domino::EvName&) const noexcept override;
    [[nodiscard]] S_PTR<void> wbasic_getData(const Domino::EvName&) const noexcept;

    [[nodiscard]] bool replaceDataOK(const Domino::EvName&, S_PTR<void> aData = nullptr) noexcept override;
    [[nodiscard]] bool wbasic_replaceDataOK(const Domino::EvName&, S_PTR<void> aData = nullptr) noexcept;

protected:
    void rmEv_(Domino::Event aValidEv) noexcept override;

private:
    // forbid ouside use base directly
    using aDominoType::getData;
    using aDominoType::replaceDataOK;
    bool isWrCtrl_(Domino::Event aEv) const noexcept { return aEv < wrCtrl_.size() ? wrCtrl_[aEv] : false; }
    // -------------------------------------------------------------------------------------------
    std::vector<bool> wrCtrl_;

public:
    using aDominoType::oneLog;
};

// ***********************************************************************************************
template<typename aDominoType>
S_PTR<void> WbasicDatDom<aDominoType>::getData(const Domino::EvName& aEvName) const noexcept
{
    if (const auto ev = this->getEventBy(aEvName); not isWrCtrl_(ev))
        return aDominoType::getData_(ev);

    WRN("(WbasicDatDom) Failed!!! EvName=" << aEvName << " is write-protect so unavailable via this func!!!");
    return nullptr;
}

// ***********************************************************************************************
template<typename aDominoType>
bool WbasicDatDom<aDominoType>::isWrCtrl(const Domino::EvName& aEvName) const noexcept
{
    return isWrCtrl_(this->getEventBy(aEvName));
}

// ***********************************************************************************************
template<typename aDominoType>
bool WbasicDatDom<aDominoType>::replaceDataOK(const Domino::EvName& aEvName, S_PTR<void> aData) noexcept
{
    if (const auto ev = this->getEventBy(aEvName); isWrCtrl_(ev)) {
        WRN("(WbasicDatDom) Failed!!! EvName=" << aEvName << " is write-protect so unavailable via this func!!!");
        return false;
    }
    else return aDominoType::replaceDataOK(aEvName, std::move(aData));
}

// ***********************************************************************************************
template<typename aDominoType>
void WbasicDatDom<aDominoType>::rmEv_(Domino::Event aValidEv) noexcept
{
    if (aValidEv < wrCtrl_.size())
        wrCtrl_[aValidEv] = false;

    aDominoType::rmEv_(aValidEv);
}

// ***********************************************************************************************
template<typename aDominoType>
S_PTR<void> WbasicDatDom<aDominoType>::wbasic_getData(const Domino::EvName& aEvName) const noexcept
{
    if (const auto ev = this->getEventBy(aEvName); isWrCtrl_(ev))
        return aDominoType::getData_(ev);

    WRN("(WbasicDatDom) Failed!!! EvName=" << aEvName << " is not write-protect so unavailable via this func!!!");
    return nullptr;
}

// ***********************************************************************************************
template<typename aDominoType>
bool WbasicDatDom<aDominoType>::wbasic_replaceDataOK(const Domino::EvName& aEvName, S_PTR<void> aData) noexcept
{
    if (const auto ev = this->getEventBy(aEvName); isWrCtrl_(ev))
        return aDominoType::replaceDataOK(aEvName, std::move(aData));
    else {
        WRN("(WbasicDatDom) Failed!!! EvName=" << aEvName << " is not write-protect so unavailable via this func!!!");
        return false;
    }
}

// ***********************************************************************************************
template<typename aDominoType>
bool WbasicDatDom<aDominoType>::wrCtrlOk(const Domino::EvName& aEvName, const bool aNewState) noexcept
{
    const auto ev = this->newEvent(aEvName);
    if (aDominoType::getData_(ev))
    {
        WRN("(WbasicDatDom) !!! Failed to change wrCtrl when already own data(out-of-ctrl), EvName=" << aEvName);
        return false;
    }

    if (ev >= wrCtrl_.size())
        wrCtrl_.resize(ev + 1);  // resize() can inc size()
    wrCtrl_[ev] = aNewState;
    HID("(WbasicDatDom) Succeed, EvName=" << aEvName << ", new wrCtrl=" << aNewState
        << ", nWr=" << wrCtrl_.size() << ", cap=" << wrCtrl_.capacity());
    return true;
}



#define EXTEND_INTERFACE_FOR_WBASIC_DATA_DOMINO  // more friendly than min WbasicDatDom interface
// ***********************************************************************************************
// - this func cast type so more convenient than WbasicDatDom's
// - SafePtr is safe, while shared_ptr maybe NOT
template<typename aDataDominoType, typename aDataType>
[[nodiscard]] S_PTR<aDataType> wbasic_getData(aDataDominoType& aDom, const Domino::EvName& aEvName) noexcept
{
    return STATIC_PTR_CAST<aDataType>(aDom.wbasic_getData(aEvName));  // mem safe: yes SafePtr, no shared_ptr
}

// ***********************************************************************************************
template<typename aDataDominoType, typename aDataType>
[[nodiscard]] bool wbasic_setValueOK(aDataDominoType& aDom, const Domino::EvName& aEvName, const aDataType& aData) noexcept
{
    return aDom.wbasic_replaceDataOK(aEvName, MAKE_PTR<aDataType>(aData));
}

}  // namespace
// ***********************************************************************************************
// - why not wbasic_setValueOK() auto call wrCtrlOk()?
//   . auto is more convient & efficient
//   . but impact wbasic_setValueOK(), wbasic_getData(), setValue(), getData(), logic complex
//   . user may mis-use setValue/getData(write-ctrl-data)
// - why also read ctrl?
//   . SIMPLY separate interface: 1)get/set legacy 2)get/set write-protected
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2021-09-18  CSZ       1)create
// 2022-01-02  PJ & CSZ  - formal log & naming
// 2022-03-26  CSZ       - ut's PARA_DOM include self class & ALL its base class(es)
// 2022-03-27  CSZ       - if ut case can test base class, never specify derive
// 2022-08-18  CSZ       - replace CppLog by UniLog
// 2022-12-03  CSZ       - simple & natural
// 2022-12-31  CSZ       - rm data
// 2024-02-12  CSZ       2)use SafePtr (mem-safe); shared_ptr is not mem-safe
// 2025-02-13  CSZ       - support both SafePtr & shared_ptr
// 2025-03-29  CSZ       3)tolerate exception
// ***********************************************************************************************
