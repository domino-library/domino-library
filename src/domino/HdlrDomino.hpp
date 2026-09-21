/**
 * Copyright 2020 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// - what issue:
//   * complex to keep hdlr in Domino
//   . state & hdlr are different core
//   . so mv hdlr out Domino
//
// - why/value:
//   . pure Domino w/o hdlr (SOLID#1: single-responsibility)
//   * basic hdlr for common usage
//     . and extendable
//   . support rm hdlr
//     . whenever succ, no cb, even cb already on road
//
// - core: ev_hdlr_S_
//
// - class safe: yes
//   . no duty to hdlr itself's any unsafe behavior
//   . mem safe: yes SafePtr, no shared_ptr
// ***********************************************************************************************
#pragma once

#include <functional>
#include <stdexcept>
#include <vector>

#include "MsgSelf.hpp"
#include "ObjAnywhere.hpp"
#include "UniLog.hpp"
#include "UniPtr.hpp"

namespace rlib
{
// ***********************************************************************************************
template<class aDominoType>
class HdlrDomino : public aDominoType
{
public:
    explicit HdlrDomino(const LogName& aUniLogName = ULN_DEFAULT);
    [[nodiscard]] bool setMsgSelfOK(const S_PTR<MsgSelf>& aMsgSelf) noexcept;  // replace default; safe: yes SafePtr, no shared_ptr

    Domino::Event setHdlr(const Domino::EvName&, MsgCB aHdlr) noexcept;
    [[nodiscard]] bool rmOneHdlrOK(const Domino::EvName&) noexcept;  // rm by EvName
    void forceAllHdlr(const Domino::EvName& aEN) noexcept { effect_(this->getEventBy(aEN)); }
    [[nodiscard]] virtual size_t nHdlr(const Domino::EvName& aEN) const noexcept { return nHdlr_(this->getEventBy(aEN)); }

    // -------------------------------------------------------------------------------------------
    // - alternative to MultiHdlrDomino::multiHdlrOnSameEv():
    //   . each hdlr owns its own event, so can independently repeatedHdlr()/rmOneHdlrOK()/etc
    //   . cons: the state of aTriggerEN & aNewEN may not always sync
    // -------------------------------------------------------------------------------------------
    Domino::Event setLinkedHdlr(const Domino::EvName& aNewEN, MsgCB aHdlr,
        const Domino::EvName& aTriggerEN) noexcept;

    [[nodiscard]] virtual EMsgPriority getPriority(Domino::Event) const noexcept { return EMsgPri_NORM; }

protected:
    void effect_(Domino::Event aEv) noexcept override;
    virtual void triggerHdlr_(const SharedMsgCB& aValidHdlr, Domino::Event aValidEv) noexcept;
    virtual bool rmOneHdlrOK_(Domino::Event aValidEv, const SharedMsgCB& aValidHdlr) noexcept;  // by aValidHdlr

    void rmEv_(Domino::Event aValidEv) noexcept override;
    size_t nHdlr_(Domino::Event aEv) const noexcept { return (aEv < ev_hdlr_S_.size() && ev_hdlr_S_[aEv]) ? 1 : 0; }
    bool rmOneHdlrOK_(Domino::Event aEv) noexcept;

    static void cb_hdlr_(HdlrDomino*, Domino::Event, const WeakMsgCB&) noexcept;

    // -------------------------------------------------------------------------------------------
private:
    // vector is faster & less mem than unordered_map when eg:
    // - nEv is not big
    // - nEv/nHdlr >> 10
    std::vector<SharedMsgCB> ev_hdlr_S_;  // [event]=hdlr; null=no hdlr
protected:
    S_PTR<MsgSelf> msgSelf_ = ObjAnywhere::getObj<MsgSelf>();
public:
    using aDominoType::oneLog;
};

// ***********************************************************************************************
template<class aDominoType>
HdlrDomino<aDominoType>::HdlrDomino(const LogName& aUniLogName) : aDominoType(aUniLogName)
{
    if (!msgSelf_)
        throw std::runtime_error("(HdlrDom) MsgSelf is required but null/absent");
}

// ***********************************************************************************************
// - static fn
// - member fn: for catch(...) to ERR()
template<class aDominoType>
void HdlrDomino<aDominoType>::cb_hdlr_(HdlrDomino* aSelfDom, Domino::Event aValidEv, const WeakMsgCB& aWeakCB) noexcept
{
    if (auto cb = aWeakCB.lock()) {  // hdlr ok -> Dom.map ok -> Dom ok
        try { (*(cb.get()))(); }  // setHdlr() forbid cb==null
        catch(...) {
            auto& oneLog = *aSelfDom;
            ERR("(HdlrDom) hdlr() except=" << mt_exceptInfo() << ", en=" << aSelfDom->evName_(aValidEv));
        }
    }
}

// ***********************************************************************************************
template<class aDominoType>
void HdlrDomino<aDominoType>::effect_(Domino::Event aEv) noexcept
{
    aDominoType::effect_(aEv);

    // validate
    if (nHdlr_(aEv) == 0)
        return;

    HID("(HdlrDom) Succeed to trigger 1 hdlr of EvName=" << this->evName_(aEv));
    triggerHdlr_(ev_hdlr_S_[aEv], aEv);
}

// ***********************************************************************************************
template<class aDominoType>
Domino::Event HdlrDomino<aDominoType>::setLinkedHdlr(const Domino::EvName& aNewEN,
    MsgCB aHdlr, const Domino::EvName& aTriggerEN) noexcept
{
    if (this->getEventBy(aNewEN) != Domino::D_EVENT_FAILED_RET)
    {
        ERR("(HdlrDom) fail since linked handler requires a new event, but en=" << aNewEN << " already exists");
        return Domino::D_EVENT_FAILED_RET;
    }
    if (! aHdlr)
    {
        WRN("(HdlrDom) Failed!!! not accept aHdlr=nullptr.");
        return Domino::D_EVENT_FAILED_RET;
    }
    if (aNewEN == aTriggerEN)
    {
        ERR("(HdlrDom) fail since linked handler can't trigger itself, en=" << aNewEN);
        return Domino::D_EVENT_FAILED_RET;
    }

    // Link first so a setPrev() failure cannot leave a handler behind.
    const auto newEv = this->setPrev(aNewEN, {{aTriggerEN, true}});
    // A fresh non-self event with one predecessor can't fail logical validation; keep defensive.
    if (newEv == Domino::D_EVENT_FAILED_RET)  // GCOVR_EXCL_BR_LINE
        return Domino::D_EVENT_FAILED_RET;  // GCOVR_EXCL_LINE

    // Preconditions above guarantee setHdlr() can install on this fresh event.
    return this->setHdlr(aNewEN, std::move(aHdlr));
}

// ***********************************************************************************************
template<class aDominoType>
bool HdlrDomino<aDominoType>::rmOneHdlrOK_(Domino::Event aEv) noexcept
{
    if (nHdlr_(aEv) > 0) {
        ev_hdlr_S_[aEv] = nullptr;
        return true;
    }
    return false;
}

// ***********************************************************************************************
template<typename aDominoType>
void HdlrDomino<aDominoType>::rmEv_(Domino::Event aValidEv) noexcept
{
    if (nHdlr_(aValidEv) > 0)
        ev_hdlr_S_[aValidEv] = nullptr;
    aDominoType::rmEv_(aValidEv);
}

// ***********************************************************************************************
template<class aDominoType>
bool HdlrDomino<aDominoType>::rmOneHdlrOK(const Domino::EvName& aEvName) noexcept
{
    HID("(HdlrDom) EvName=" << aEvName);
    return rmOneHdlrOK_(this->getEventBy(aEvName));
}

// ***********************************************************************************************
template<class aDominoType>
bool HdlrDomino<aDominoType>::rmOneHdlrOK_(Domino::Event aValidEv, const SharedMsgCB& aValidHdlr) noexcept
{
    if (nHdlr_(aValidEv) == 0)
        return false;

    // req: must match
    if (ev_hdlr_S_[aValidEv] != aValidHdlr)
        return false;

    HID("(HdlrDom) Will remove hdlr of EvName=" << this->evName_(aValidEv)
        << ", nHdlrRef=" << ev_hdlr_S_[aValidEv].use_count());
    ev_hdlr_S_[aValidEv] = nullptr;
    return true;
}

// ***********************************************************************************************
template<class aDominoType>
Domino::Event HdlrDomino<aDominoType>::setHdlr(const Domino::EvName& aEvName, MsgCB aHdlr) noexcept
{
    // validate
    if (! aHdlr)
    {
        WRN("(HdlrDom) Failed!!! not accept aHdlr=nullptr.");
        return Domino::D_EVENT_FAILED_RET;
    }
    auto&& newEv = this->newEvent(aEvName);
    if (nHdlr_(newEv) > 0)
    {
        ERR("(HdlrDom) Failed!!! Can't overwrite hdlr for " << aEvName << ". Rm old or Use MultiHdlrDomino instead.");
        return Domino::D_EVENT_FAILED_RET;
    }

    // set
    auto newHdlr = MAKE_PTR<MsgCB>(std::move(aHdlr));
    if (newEv >= ev_hdlr_S_.size())
        ev_hdlr_S_.resize(newEv + 1);
    ev_hdlr_S_[newEv] = newHdlr;
    HID("(HdlrDom) Succeed for EvName=" << aEvName);

    // call
    if (this->state(newEv) == true)
    {
        HID("(HdlrDom) Trigger the new hdlr of EvName=" << aEvName);
        triggerHdlr_(newHdlr, newEv);
    }
    return newEv;
}

// ***********************************************************************************************
template<class aDominoType>
bool HdlrDomino<aDominoType>::setMsgSelfOK(const S_PTR<MsgSelf>& aMsgSelf) noexcept
{
    // validate
    // The constructor and this setter guarantee msgSelf_ is never null.
    const auto nMsgUnhandled = msgSelf_->nMsg();
    if (nMsgUnhandled > 0)
    {
        ERR("(MsgSelf) failed!!! since old msgSelf is not empty, nMsgUnhandled=" << nMsgUnhandled);
        return false;
    }
    if (aMsgSelf.get() == nullptr)
    {
        ERR("Failed!!! since HdlrDomino unsafe when msgSelf==nullptr");
        return false;
    }

    // set
    msgSelf_ = aMsgSelf;
    return true;
}

// ***********************************************************************************************
template<class aDominoType>
void HdlrDomino<aDominoType>::triggerHdlr_(const SharedMsgCB& aValidHdlr, Domino::Event aValidEv) noexcept
{
    HID("(HdlrDom) trigger a new msg.");
    if (!msgSelf_->newMsgOK(
        [aSelfDom = this, aValidEv, weakMsgCB = WeakMsgCB(aValidHdlr)]() noexcept {
            cb_hdlr_(aSelfDom, aValidEv, weakMsgCB);  // not exe here
        },
        getPriority(aValidEv)
    ))
    {
        ERR("(HdlrDom) Failed to newMsgOK for en=" << this->evName_(aValidEv));
    }
}

}  // namespace
// ***********************************************************************************************
// YYYY-MM-DD  Who       v)Modification Description
// ..........  .........   ......................................................................
// 2020-12-11  CSZ       1)create
// 2020-12-28  CSZ       2)fix invalid cb by weak_ptr<cb>
// 2021-04-01  CSZ       - coding req
// 2022-01-04  PJ & CSZ  - formal log & naming
// 2022-03-26  CSZ       - ut's PARA_DOM include self class & ALL its base class(es)
// 2022-03-27  CSZ       - if ut case can test base class, never specify derive
// 2022-08-18  CSZ       - replace CppLog by UniLog
// 2023-01-23  CSZ       - rename pureRmHdlrOK to rmOneHdlrOK
// 2023-05-24  CSZ       - support force call hdlr
// 2024-03-10  CSZ       - enhance safe eg setMsgSelf()
// 2025-02-13  CSZ       - support both SafePtr & shared_ptr
// 2025-04-05  CSZ       3)tolerate exception
// ***********************************************************************************************
