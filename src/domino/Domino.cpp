/**
 * Copyright 2018 Nokia. All rights reserved.
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <algorithm>
#include <cctype>
#include <string>

#include "Domino.hpp"

using namespace std;

namespace rlib
{
static const Domino::EVs defaultEvPeers;  // internal use only

// ***********************************************************************************************
void Domino::deduceStateFrom_(Event aValidEv) noexcept
{
    stack<Event> evStack;
    for (auto curEV = aValidEv; ; curEV = evStack.top(), evStack.pop())
    {
        HID("(Domino) en=" << evName_(curEV));

        // recalc state from predecessors
        if (auto newState = deduceStateSelf_(curEV, true) && deduceStateSelf_(curEV, false);
            pureSetStateOK_(curEV, newState))  // state changed
        {
            // propagate to successors
            for (bool branch : {true, false}) {  // search next_[true] & next_[false]
                for (auto&& nextEV : findPeerEVs(curEV, next_[branch])) {
                    evStack.push(nextEV);  // dup-deduce is safer (like real domino)
                }
            }
        }

        if (evStack.empty())
            return;
    }
}

// ***********************************************************************************************
bool Domino::deduceStateSelf_(Event aValidEv, bool aPrevType) const noexcept
{
    for (auto&& prevEV : findPeerEVs(aValidEv, prev_[aPrevType]))
        if (states_[prevEV] != aPrevType)  // 1 prev not satisfied
            return false;
    return true;
}

// ***********************************************************************************************
void Domino::effect_() noexcept
{
    for (auto&& ev : effectEVs_)
        if (states_[ev] == true)  // avoid multi-change; skip bounds check since effectEVs_ are validated
            effect_(ev);
    decltype(effectEVs_)().swap(effectEVs_);  // may safer & faster than clear()
}

// ***********************************************************************************************
Domino::EvNames Domino::evNames() const noexcept
{
    EvNames names;
    names.reserve(en_ev_.size());
    for (auto&& [name, ev] : en_ev_)
        if (!isRemoved(ev)) names.push_back(name);
    return names;
}

// ***********************************************************************************************
const Domino::EVs& Domino::findPeerEVs(Event aEv, const EvLinks& aLinks) noexcept
{
    return aEv < aLinks.size() ? aLinks[aEv] : defaultEvPeers;
}

// ***********************************************************************************************
Domino::Event Domino::getEventBy(const EvName& aEvName) const noexcept
{
    auto&& en_ev = en_ev_.find(aEvName);
    return en_ev == en_ev_.end()
        ? D_EVENT_FAILED_RET
        : en_ev->second;
}

// ***********************************************************************************************
Domino::Event Domino::newEvent(const EvName& aEvName) noexcept
{
#ifdef IN_ALL_UT
    if (newEvHook_forUt)
        newEvHook_forUt(*this, aEvName);
#endif
    if (!aEvName.empty() &&  // otherwise isspace() may UB
        (isspace(static_cast<unsigned char>(aEvName.front())) || isspace(static_cast<unsigned char>(aEvName.back())))
    )
        WRN("(Domino) EvName has leading/trailing whitespace: '" << aEvName << "'");

    // exist?
    auto&& newEv = getEventBy(aEvName);
    if (newEv != D_EVENT_FAILED_RET)
        return newEv;

    // new from rm-ed
    newEv = recycleEv_();
    if (newEv == D_EVENT_FAILED_RET)
        newEv = states_.size();

    HID("(Domino) init new EvName=" << aEvName << ", event=" << newEv);
    en_ev_[aEvName] = newEv;
    if (newEv >= states_.size()) {
        states_.push_back(false);  // create new slot
        ev_en_.emplace_back();  // allocate space
        for (auto& link : prev_) link.emplace_back();
        for (auto& link : next_) link.emplace_back();
    }
    ev_en_[newEv] = aEvName;

    return newEv;
}

// ***********************************************************************************************
void Domino::pureRmLink_(Event aValidEv, EvLinks& aMyLinks, EvLinks& aNeighborLinks) noexcept
{
    // rm neighbor's link
    for (auto&& peerEv : findPeerEVs(aValidEv, aMyLinks))
    {
        if (peerEv >= aNeighborLinks.size())  // not found
            continue;
        auto&& peers = aNeighborLinks[peerEv];
        if (peers.size() <= 1)
            peers.clear();  // erase entire
        else {
            auto&& pos = find(peers.begin(), peers.end(), aValidEv);
            if (pos != peers.end()) {  // swap-erase
                *pos = peers.back();
                peers.pop_back();
            }
        }
    }

    // rm my link
    if (aValidEv < aMyLinks.size())
        aMyLinks[aValidEv].clear();
}

// ***********************************************************************************************
void Domino::pureSetPrev_(Event aValidEv, const SimuEvents& aSimuPrevEvents) noexcept
{
    HID("(Domino) before: nPrev[true]=" << prev_[true].size() << ", nNext[true]=" << next_[true].size()
        << ", nPrev[false]=" << prev_[false].size() << ", nNext[false]=" << next_[false].size());
    for (auto&& [prevEn, state] : aSimuPrevEvents)
    {
        auto&& prevEv = newEvent(prevEn);
        auto&& prevPeers = prev_[state][aValidEv];
        if (find(prevPeers.begin(), prevPeers.end(), prevEv) == prevPeers.end()) {
            prevPeers.push_back(prevEv);
            TRC("(Domino) %s %s %s", prevEn.c_str(),
                state ? "-T->" : "-F->", evName_(aValidEv).c_str());
        }
        auto&& nextPeers = next_[state][prevEv];
        if (find(nextPeers.begin(), nextPeers.end(), aValidEv) == nextPeers.end())
            nextPeers.push_back(aValidEv);
    }
    HID("(Domino) after: nPrev[true]=" << prev_[true].size() << ", nNext[true]=" << next_[true].size()
        << ", nPrev[false]=" << prev_[false].size() << ", nNext[false]=" << next_[false].size());
}

// ***********************************************************************************************
bool Domino::pureSetStateOK_(Event aValidEv, const bool aNewState) noexcept
{
    if (states_[aValidEv] != aNewState)  // do need change
    {
        states_[aValidEv] = aNewState;
        TRC("(Domino) %s=%c", evName_(aValidEv).c_str(), aNewState ? 'T' : 'F');
        if (aNewState == true)
            effectEVs_.push_back(aValidEv);
        return true;
    }
    return false;
}

// ***********************************************************************************************
void Domino::rmEv_(Event aValidEv) noexcept
{
    // cp for later deduceStateFrom_(next)
    auto trueNextEVs  = findPeerEVs(aValidEv, next_[true]);
    auto falseNextEVs = findPeerEVs(aValidEv, next_[false]);
    HID("(Domino) en=" << evName_(aValidEv) << ", nNextT=" << trueNextEVs.size() << ", nNextF=" << falseNextEVs.size());

    // rm link
    pureRmLink_(aValidEv, prev_[true],  next_[true]);
    pureRmLink_(aValidEv, prev_[false], next_[false]);
    pureRmLink_(aValidEv, next_[true],  prev_[true]);
    pureRmLink_(aValidEv, next_[false], prev_[false]);

    // rm self resrc
    pureSetStateOK_(aValidEv, false);  // must before clean ev_en_
    en_ev_.erase(evName_(aValidEv));
    ev_en_[aValidEv].clear();
    HID("[Domino] ev=" << aValidEv);

    // deduce impacted; safer to dup-deduce same ev
    for (auto&& nextEV : trueNextEVs)
        deduceStateFrom_(nextEV);
    for (auto&& nextEV : falseNextEVs)
        deduceStateFrom_(nextEV);

    // call hdlr
    effect_();
}

// ***********************************************************************************************
Domino::Event Domino::setPrev(const EvName& aEvName, const SimuEvents& aSimuPrevEvents) noexcept
{
    const auto fromEv = newEvent(aEvName);  // complex by getEventBy(), not worth
    // - compute all nextable events from fromEv once for all aSimuPrevEvents
    // - vector<bool>/bit is safer than unordered_set when huge nexts
    vector<bool> nextable(states_.size() + aSimuPrevEvents.size(), false);  // reserve & init; tmp container
    {
        stack<Event> evStack;
        nextable[fromEv] = true;
        for (auto curEv = fromEv; ; curEv = evStack.top(), evStack.pop())
        {
            for (bool branch : {true, false}) {
                for (auto&& nextEV : findPeerEVs(curEv, next_[branch])) {
                    if (!nextable[nextEV]) {
                        nextable[nextEV] = true;  // mark-on-push(than mark-on-pop), avoid dup push & infinite loop
                        evStack.push(nextEV);
                    }
                }
            }
            if (evStack.empty())
                break;
        }
    }
    // validate loop & conflict
    for (auto&& [prevEn, state] : aSimuPrevEvents)
    {
        auto&& prevEv = newEvent(prevEn);
        if (nextable[prevEv])  // + aSimuPrevEvents.size() so impossible out-bounds
        {
            ERR("(Domino) !!!Failed since invalid EN=" << aEvName << ", or loop to=" << prevEn);
            return D_EVENT_FAILED_RET;
        }
        auto&& conflictPeers = findPeerEVs(fromEv, prev_[!state]);
        if (find(conflictPeers.begin(), conflictPeers.end(), prevEv) != conflictPeers.end())
        {
            ERR("(Domino) !!!Failed since T/F conflict on prev=" << prevEn << " for " << aEvName);
            return D_EVENT_FAILED_RET;
        }
    }

    // set prev
    pureSetPrev_(fromEv, aSimuPrevEvents);

    // deduce all impacted
    deduceStateFrom_(fromEv);

    // call hdlr
    effect_();
    return fromEv;
}

// ***********************************************************************************************
size_t Domino::setState(const SimuEvents& aSimuEvents)
{
    // validate
    for (auto&& [en, state] : aSimuEvents)
    {
        const auto ev = getEventBy(en);  // not create new ev if validation fail
        if (ev == D_EVENT_FAILED_RET)
            continue;  // new ev, need to create in next step
        if ((ev < prev_[true].size() && !prev_[true][ev].empty()) || (ev < prev_[false].size() && !prev_[false][ev].empty()))
        {
            ERR("(Domino) refuse since en=" << en << " has prev (avoid break its prev logic)");
            return 0;
        }
    }

    // set ALL state(s) before deduce
    EVs simuEVs;
    simuEVs.reserve(aSimuEvents.size());
    for (auto&& [en, state] : aSimuEvents)
    {
        if (const auto ev = newEvent(en); pureSetStateOK_(ev, state)) {  // real changed
            simuEVs.push_back(ev);  // no dup: map keys unique + pureSetStateOK_
        }
    }

    // deduce next(s)
    for (auto&& curEV : simuEVs) {
        for (bool branch : {true, false}) {
            for (auto&& nextEV : findPeerEVs(curEV, next_[branch])) {
                deduceStateFrom_(nextEV);  // dup-deduce is safer (like real domino)
            }
        }
    }

    // safer to call hdlr(s) after deduce
    effect_();
    return simuEVs.size();  // real changed
}

// ***********************************************************************************************
Domino::EvName Domino::whyFalse(Event aEv) const noexcept
{
    // validate to safe public interface
    if (isRemoved(aEv))
    {
        WRN("(Domino) invalid event=" << aEv);
        return EvName(DOM_RESERVED_EVNAME) + " whyFalse() found nothing";
    }
    if (state(aEv) == true)
    {
        WRN("(Domino) en=" << ev_en_[aEv] << ", state=true");
        return EvName(DOM_RESERVED_EVNAME) + " whyFalse() found nothing";
    }
    HID("(Domino) en=" << ev_en_[aEv]);

    // loop search
    WhyStep step{aEv, false, EvName()};
    while (step.resultEN_.empty()) {
        step.whyFlag_ ? whyTrue_ (step) : whyFalse_(step);
    }
    return step.resultEN_;
}
void Domino::whyFalse_(WhyStep& aStep) const noexcept
{
    EVs::const_iterator it;
    // search true prev
    for (auto curEV = aStep.curEV_;; curEV = *it) {
        auto&& prevEVs = findPeerEVs(curEV, prev_[true]);
        it = find_if(prevEVs.begin(), prevEVs.end(),
            [this](auto&& aPrevEV) noexcept { return states_[aPrevEV] == false; });
        if (it == prevEVs.end()) {  // nothing in true-prev
            if (curEV == aStep.curEV_) {
                break;  // try false-prev
            }

            aStep.resultEN_ = evName_(curEV) + "==false";  // found
            HID("(Domino) found false en=" << evName_(curEV) << " from true prevEVs=" << prevEVs.size());
            return;
        }
    }

    // search false prev
    auto&& prevEVs = findPeerEVs(aStep.curEV_, prev_[false]);
    it = find_if(prevEVs.begin(), prevEVs.end(),
        [this](auto&& aPrevEV) noexcept { return states_[aPrevEV] == true; });
    if (it == prevEVs.end()) {  // nothing in false-prev
        HID("(Domino) found true en=" << evName_(aStep.curEV_) << " from false prevEVs=" << prevEVs.size());
        aStep.resultEN_ = evName_(aStep.curEV_) + "==false";
        return;
    }
    // found true-ev in false-prev, next whyTrue_()
    aStep.curEV_ = *it;
    aStep.whyFlag_ = true;
}
void Domino::whyTrue_(WhyStep& aStep) const noexcept
{
    for (;;) {
        auto&&  truePrevEVs = findPeerEVs(aStep.curEV_, prev_[true]);
        auto&& falsePrevEVs = findPeerEVs(aStep.curEV_, prev_[false]);

        HID("(Domino en=" << evName_(aStep.curEV_) << ", nTruePrev=" << truePrevEVs.size()
            << ", nFalsePrev=" << falsePrevEVs.size());
        if (truePrevEVs.size() == 1 && falsePrevEVs.empty()) {
            aStep.curEV_ = *(truePrevEVs.begin());
            continue;
        }
        if (truePrevEVs.empty() && falsePrevEVs.size() == 1) {
            aStep.curEV_ = *(falsePrevEVs.begin());
            aStep.whyFlag_ = false;
            return;  // next whyFalse_()
        }
        // found true-ev with 0-prev/multi-prev, stop here for single root cause
        HID("(Domino) found true en=" << evName_(aStep.curEV_));
        aStep.resultEN_ = evName_(aStep.curEV_) + "==true";
        return;
    }
}

}  // namespace
