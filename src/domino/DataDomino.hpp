/**
 * Copyright 2020 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - ISSUE:    eg C&M translator store any type data from RU/BBU/HLAPI, like IM [MUST-HAVE!]
// - Req:      Domino to store any type of data
// - core:     ev_data_S_
// - scope:    provide min interface (& extend by non-member func)
// - mem-safe: true (when use SafePtr instead of shared_ptr)
// ***********************************************************************************************
#pragma once

#include "DataStore.hpp"
#include "UniLog.hpp"
#include "UniPtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<typename aDominoType>
class DataDomino : public aDominoType
{
    // -------------------------------------------------------------------------------------------
    // Extend Tile record:
    // - data: now Tile can store any type of data besides state
    // - data is optional of Tile
    // -------------------------------------------------------------------------------------------

public:
    explicit DataDomino(const LogName& aUniLogName = ULN_DEFAULT) : aDominoType(aUniLogName) {}

    // -------------------------------------------------------------------------------------------
    // - for read/write data
    // - if aEvName invalid, return null
    // - not template<aDataType> so can virtual for WrDatDom
    // . & let DataDomino has little idea of read-write ctrl, simpler
    [[nodiscard]] virtual S_PTR<void> getData(const Domino::EvName&) const noexcept;

    // -------------------------------------------------------------------------------------------
    // - replace old data by new=aData if old != new
    // - for aDataType w/o default constructor!!!
    // - ret true=succ, false=fail
    [[nodiscard]] virtual bool replaceDataOK(const Domino::EvName&, S_PTR<void> = nullptr) noexcept;

protected:
    void rmEv_(Domino::Event aValidEv) noexcept override;
    S_PTR<void> getData_(Domino::Event aEv) const noexcept { return ev_data_S_.get<void>(aEv); }
    bool replaceDataOK_(Domino::Event aEv, S_PTR<void> aData) noexcept { return ev_data_S_.replaceOK(aEv, std::move(aData)); }

private:
    // -------------------------------------------------------------------------------------------
    DataStore<Domino::Event> ev_data_S_;  // [event]=S_PTR<void>
};

// ***********************************************************************************************
template<typename aDominoType>
S_PTR<void> DataDomino<aDominoType>::getData(const Domino::EvName& aEvName) const noexcept
{
    return ev_data_S_.get<void>(this->getEventBy(aEvName));
}

// ***********************************************************************************************
template<typename aDominoType>
bool DataDomino<aDominoType>::replaceDataOK(const Domino::EvName& aEvName, S_PTR<void> aData) noexcept
{
    return ev_data_S_.replaceOK(this->newEvent(aEvName), std::move(aData));
}

// ***********************************************************************************************
template<typename aDominoType>
void DataDomino<aDominoType>::rmEv_(Domino::Event aValidEv) noexcept
{
    const auto replaced = ev_data_S_.replaceOK(aValidEv, nullptr);
    (void)replaced;
    aDominoType::rmEv_(aValidEv);
}


#define EXTEND_INTERFACE_FOR_DATA_DOMINO  // more friendly with min DataDomino interface
// ***********************************************************************************************
// - this getData() cast type so convenient
// - SafePtr is safe, while shared_ptr maybe NOT
template<typename aDataDominoType, typename aDataType>
[[nodiscard]] S_PTR<aDataType> getData(aDataDominoType& aDom, const Domino::EvName& aEvName) noexcept
{
    return STATIC_PTR_CAST<aDataType>(aDom.getData(aEvName));
}

// ***********************************************************************************************
template<typename aDataDominoType, typename aDataType>
[[nodiscard]] bool setValueOK(aDataDominoType& aDom, const Domino::EvName& aEvName, const aDataType& aData) noexcept
{
    return aDom.replaceDataOK(aEvName, MAKE_PTR<aDataType>(aData));
}
}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2020-07-27  CSZ       1)create
// 2021-04-01  CSZ       - coding req
// 2021-04-30  ZQ & CSZ  2)ZhangQiang told CSZ shared_ptr<void> can del Data correctly
// 2021-09-13  CSZ       - new interface to read/write value directly
// 2021-09-14  CSZ       3)writable ctrl eg for Yang rw & ro para
// 2021-12-31  PJ & CSZ  - formal log & naming
// 2022-03-26  CSZ       - ut's PARA_DOM include self class & ALL its base class(es)
// 2022-03-27  CSZ       - if ut case can test base class, never specify derive
// 2022-08-18  CSZ       - replace CppLog by UniLog
// 2022-12-03  CSZ       - simple & natural
// 2022-12-30  CSZ       - rm data
// 2024-02-12  CSZ       4)use SafePtr (mem-safe); shared_ptr is not mem-safe
// 2024-06-08  CSZ       5)use DataStore instead of map
// 2025-02-13  CSZ       - support both SafePtr & shared_ptr
// 2025-03-29  CSZ       6)tolerate exception
// ***********************************************************************************************
