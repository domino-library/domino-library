/**
 * Copyright 2024 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - REQ: this class is to
//   . store data of any type: eg for ObjAnywhere
//   * store multi-copy of data of the same type: eg for DataDomino
//   . fetch data by unique key(eg string)
//
// - MT safe: NO
// - mem safe: yes if SafePtr
// ***********************************************************************************************
#pragma once

#include <unordered_map>

#include "UniPtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<typename aDataKey>
class DataStore
{
public:
    // @brief: get a data
    // @ret: ok or nullptr
    template<typename aDataT> [[nodiscard]] SPtr<aDataT> get(const aDataKey& aKey) const noexcept;

    // @brief: store a new data (ie not replace but add new or rm old by aData=nullptr)
    // @ret: store ok/nok
    [[nodiscard]] bool emplaceOK(const aDataKey& aKey, SPtr<void> aData) noexcept;

    // @brief: store a data (add new, replace old, or rm old by aData=nullptr)
    // @ret: store ok/nok
    [[nodiscard]] bool replaceOK(const aDataKey& aKey, SPtr<void> aData) noexcept;

    [[nodiscard]] size_t nData() const noexcept { return key_data_S_.size(); }
    ~DataStore() noexcept { HID("(DataStore) discard nData=" << nData()); }  // debug

private:
    // -------------------------------------------------------------------------------------------
    std::unordered_map<aDataKey, SPtr<void>> key_data_S_;
};

// ***********************************************************************************************
template<typename aDataKey>
bool DataStore<aDataKey>::emplaceOK(const aDataKey& aKey, SPtr<void> aData) noexcept
{
    try {
        if (! aData)
        {
            HID("(DataStore) erase key=" << typeid(aDataKey).name());
            return key_data_S_.erase(aKey) > 0;
        }
        else
        {
            if (key_data_S_.try_emplace(aKey, std::move(aData)).second)
                return true;
            else
            {
                HID("(DataStore) ERR!!! not support to replace key=" << typeid(aDataKey).name());
                return false;
            }
        }
    } catch(...) {
        EMG("(DataStore) except=%s, key=%s", mt_exceptInfo(), typeid(aDataKey).name());
        return false;
    }
}

// ***********************************************************************************************
template<typename aDataKey>
template<typename aDataT>
SPtr<aDataT> DataStore<aDataKey>::get(const aDataKey& aKey) const noexcept
{
    try {
        const auto key_data = key_data_S_.find(aKey);
        if (key_data == key_data_S_.end())
        {
            HID("(DataStore) can't find key=" << typeid(aDataKey).name());
            return nullptr;
        }
        return static_ptr_cast<aDataT>(key_data->second);  // mem safe: yes SafePtr, no shared_ptr
    } catch(...) {
        EMG("(DataStore) except=%s, key=%s", mt_exceptInfo(), typeid(aDataKey).name());
        return nullptr;
    }
}

// ***********************************************************************************************
template<typename aDataKey>
bool DataStore<aDataKey>::replaceOK(const aDataKey& aKey, SPtr<void> aData) noexcept
{
    try {
        if (! aData)
            return emplaceOK(aKey, nullptr);

        key_data_S_.insert_or_assign(aKey, std::move(aData));
        return true;
    } catch(...) {
        EMG("(DataStore) except=%s, key=%s", mt_exceptInfo(), typeid(aDataKey).name());
        return false;
    }
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   .......................................................................
// 2024-06-05  CSZ       1)create
// 2025-02-13  CSZ       - support both SafePtr & shared_ptr
// ***********************************************************************************************
