/**
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
// Why/value:
// - heavy load: one dest, many subDest, many files, many attrs — pressure OK to DOM?
// - shuffled attr order, repeated abort, and pong steps (repeatable)
// - every handler on this path is installed
// ***********************************************************************************************
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "UtInitObjAnywhere.hpp"

namespace rlib
{
// ***********************************************************************************************
constexpr size_t nSub        = 50;   // 2 big, the rest small
constexpr size_t nSubBig     = 2;
constexpr size_t nFileBig    = 500;
constexpr size_t nFileSmall  = 10;
constexpr size_t nAttrInFile = 10;
constexpr size_t nEvAttr     = (nSubBig * nFileBig + (nSub - nSubBig) * nFileSmall) * nAttrInFile;  // shuffled setState()
constexpr size_t nFileAll    = nEvAttr / nAttrInFile;
constexpr size_t nMsgPong    = nEvAttr / 10;  // share of evOrder; each one calls pongMsgSelf_()
constexpr size_t nAbortReq   = 3;             // repeated abortReq in evOrder
constexpr size_t idAbortReq  = nEvAttr;       // evOrder value: setState abortReq
constexpr size_t idMsgPong   = nEvAttr + 1;   // evOrder value: pongMsgSelf_()

// dest
const std::string enAbortReq       = "[/dest/abortReq]";      // outside setState; file abort watches this
const std::string enAbortExe       = "[/dest/abortExe]";      // HIGH; runs when abortReq && step_0
const std::string enStep_0         = "[/dest/step_0]";
const std::string enSigSwitch      = "[/dest/sigSwitch]";     // true -> file sigA; false -> file sigB
const std::string enBarExe         = "[/dest/barExe]";        // bar may proceed
const std::string enStep_1         = "[/dest/step_1]";        // sigSwitch && step_0
const std::string enAllSubDone     = "[/dest/allSubDone]";
const std::string enStep_post      = "[/dest/step_post]";
const std::string enLogExe         = "[/dest/logExe]";
const std::string enLogDone        = "[/dest/logDone]";
const std::string enReportExe      = "[/dest/reportExe]";
const std::string enPreCheck       = "[/dest/precheck]";
const std::string enPrecheckReport = "[/dest/precheckReport]";
const std::string enSucc           = "[/dest/succ]";
const std::string enFail           = "[/dest/fail]";

// bar. update is LOW and waits for allow && changed.
const std::string enBarAllow   = "[/bar/allow]";
const std::string enBarChanged = "[/bar/changed]";
const std::string enBarUpdate  = "[/bar/update]";

// subDest leaves, under /dest/subDest-XXXX/<leaf>
const char SubStep_0[]     = "step_0";
const char SubStep_fnc[]   = "step_fnc";
const char SubSigReq[]     = "sigReq";
const char SubSigExe[]     = "sigExe";
const char SubSigDone[]    = "sigDone";
const char SubSumFnc[]     = "sumFnc";
const char SubPrepReq[]    = "prepReq";
const char SubPrepExe[]    = "prepExe";
const char SubPrepDone[]   = "prepDone";
const char SubAllFileDone[]= "allFileDone";
const char SubStep1_done[] = "step1_done";
const char SubPostExe[]    = "postExe";
const char SubPostDone[]   = "postDone";
const char SubCleanExe[]   = "cleanExe";
const char SubCleanDone[]  = "cleanDone";
const char SubSaveExe[]    = "saveExe";
const char SubSaveDone[]   = "saveDone";
const char SubEnd[]        = "end";

// file leaves, under /dest/subDest-XXXX/file-YYYY/<leaf>
const char FileDown[]  = "down";
const char FileJoin[]  = "join";
const char FileSigA[]  = "sigA";
const char FileSigB[]  = "sigB";
const char FileDistr[] = "distr";
const char FileDone[]  = "done";
const char FileAbort[] = "abort";

// Six parallel fileQuos. 0..2 are set at initTrueEV; 3..5 stay false.
// fileQuo0 is also the per-file input of subDest sumFnc.
// Each subDest fileQuoN is sumFnc AND every file's fileQuoN.
const char* const FileQuoS[] = {"fileQuo0", "fileQuo1", "fileQuo2", "fileQuo3", "fileQuo4", "fileQuo5"};
constexpr size_t nFileQuo = 6;
constexpr size_t nFileQuoOn = 3;  // fileQuo0..fileQuo2

size_t nFile(size_t aSub) { return aSub < nSubBig ? nFileBig : nFileSmall; }

std::string four(size_t aN)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04zu", aN);
    return buf;
}
std::string subPath(size_t aSub) { return "/dest/subDest-" + four(aSub); }
std::string filePath(size_t aSub, size_t aFile) { return subPath(aSub) + "/file-" + four(aFile); }
std::string enSub(const char* aLeaf, size_t aSub) { return "[" + subPath(aSub) + "/" + aLeaf + "]"; }
std::string enFile(const char* aLeaf, size_t aSub, size_t aFile)
{
    return "[" + filePath(aSub, aFile) + "/" + aLeaf + "]";
}
std::string enAttr(size_t aSub, size_t aFile, size_t aAttr)
{
    return "[" + filePath(aSub, aFile) + "/attr/" + four(aAttr) + "]";
}

// ***********************************************************************************************
template<class aDom> struct LoadTest : UtInitObjAnywhere
{
    struct AttrInfo
    {
        std::string enAttr_;
        std::string enFileDistr_;
        std::string enFileDone_;
    };

    std::shared_ptr<aDom> dom_;
    MsgCB barHdlr_;
    std::vector<AttrInfo> attrInfoS_;

    size_t nBar_ = 0;               // progress ticks; expectBar() is 100%

    // -------------------------------------------------------------------------------------------
    bool okPrev(const std::string& aEN, Domino::SimuEvents aPrev)
    {
        return dom_->setPrev(aEN, aPrev) != Domino::D_EVENT_FAILED_RET;
    }
    bool okPri(const std::string& aEN, EMsgPriority aPri)
    {
        return dom_->setPriority(aEN, aPri) != Domino::D_EVENT_FAILED_RET;
    }
    bool okHdlr(const std::string& aEN, MsgCB aHdlr)
    {
        return dom_->setHdlr(aEN, std::move(aHdlr)) != Domino::D_EVENT_FAILED_RET;
    }
    bool onHdlr(const std::string& aEN) { return okHdlr(aEN, []{}); }
    bool okBar(const std::string& aTrigger)
    {
        // A new event, so this handler can be removed without touching the trigger's own handler.
        return dom_->setLinkedHdlr("[/bar]" + aTrigger, barHdlr_, aTrigger) != Domino::D_EVENT_FAILED_RET;
    }
    // attr bars + file succ bars + each subDest's five Done bars + [/dest/logDone]
    size_t expectBar() const
    {
        return nEvAttr + nFileAll + 5u * nSub + 1u;
    }
    void drain() { this->pongMsgSelf_(); }

    void initDestS()
    {
        ASSERT_TRUE(okPri(enAbortExe, EMsgPri_HIGH));
        ASSERT_TRUE(okPrev(enAbortExe, {{enAbortReq, true}, {enStep_0, true}}));
        ASSERT_TRUE(onHdlr(enAbortExe));

        ASSERT_TRUE(okPrev(enStep_1, {{enSigSwitch, true}, {enStep_0, true}}));
        ASSERT_TRUE(onHdlr(enStep_1));

        ASSERT_TRUE(okPrev(enAllSubDone, {{enStep_0, true}}));
        ASSERT_TRUE(okPrev(enStep_post, {{enAllSubDone, true}}));
        ASSERT_TRUE(onHdlr(enStep_post));

        ASSERT_TRUE(onHdlr(enLogExe));
        ASSERT_TRUE(okPrev(enLogDone, {{enLogExe, true}}));
        ASSERT_TRUE(okBar(enLogDone));

        ASSERT_TRUE(okPrev(enReportExe, {{enLogDone, true}, {enPreCheck, false}}));
        ASSERT_TRUE(onHdlr(enReportExe));
        ASSERT_TRUE(okPrev(enPrecheckReport, {{enLogDone, true}, {enPreCheck, true}}));
        ASSERT_TRUE(onHdlr(enPrecheckReport));

        ASSERT_TRUE(okPrev(enSucc, {{enReportExe, true}, {enAbortReq, false}}));
        ASSERT_TRUE(okPrev(enFail, {{enReportExe, true}, {enAbortReq, true}}));

        ASSERT_TRUE(okPrev(enBarAllow, {{enStep_0, true}, {enBarExe, true}}));
        ASSERT_TRUE(okPri(enBarUpdate, EMsgPri_LOW));
        ASSERT_TRUE(okPrev(enBarUpdate, {{enBarAllow, true}, {enBarChanged, true}}));
        ASSERT_TRUE(onHdlr(enBarUpdate));
    }

    // Exe owns the handler. Done is deduced from Exe and carries the bar.
    bool linkExe(const std::string& aExe, const std::string& aDone, Domino::SimuEvents aPrev)
    {
        return okPrev(aExe, std::move(aPrev)) && onHdlr(aExe)
            && okPrev(aDone, {{aExe, true}}) && okBar(aDone);
    }

    void initSubS(size_t aSub)
    {
        const auto enOpen       = enSub(SubStep_0, aSub);
        const auto enCheckExe   = enSub(SubSigExe, aSub);
        const auto enCheckDone  = enSub(SubSigDone, aSub);
        const auto enJudgeExe   = enSub(SubSumFnc, aSub);
        const auto enPrepExe    = enSub(SubPrepExe, aSub);
        const auto enPrepDone   = enSub(SubPrepDone, aSub);
        const auto enAllFileExe = enSub(SubAllFileDone, aSub);
        const auto enSubDone    = enSub(SubStep1_done, aSub);
        const auto enPostExe    = enSub(SubPostExe, aSub);
        const auto enPostDone   = enSub(SubPostDone, aSub);
        const auto enCleanExe   = enSub(SubCleanExe, aSub);
        const auto enCleanDone  = enSub(SubCleanDone, aSub);
        const auto enSaveExe    = enSub(SubSaveExe, aSub);
        const auto enSaveDone   = enSub(SubSaveDone, aSub);
        const auto enSubEnd     = enSub(SubEnd, aSub);

        ASSERT_TRUE(okPrev(enSub(SubStep_fnc, aSub), {{enStep_0, true}}));
        ASSERT_TRUE(onHdlr(enSub(SubStep_fnc, aSub)));
        ASSERT_TRUE(linkExe(enCheckExe, enCheckDone, {{enOpen, true}, {enSub(SubSigReq, aSub), true}}));

        ASSERT_TRUE(okPrev(enJudgeExe, {{enOpen, true}}));
        ASSERT_TRUE(onHdlr(enJudgeExe));
        for (size_t i = 0; i < nFileQuo; ++i)
            ASSERT_TRUE(okPrev(enSub(FileQuoS[i], aSub), {{enJudgeExe, true}}));

        ASSERT_TRUE(linkExe(enPrepExe, enPrepDone, {{enSub(SubPrepReq, aSub), true}}));

        ASSERT_TRUE(okPrev(enAllFileExe, {{enOpen, true}}));
        ASSERT_TRUE(onHdlr(enAllFileExe));
        ASSERT_TRUE(okPrev(enSubDone, {{enAllFileExe, true}}));

        ASSERT_TRUE(linkExe(enPostExe, enPostDone, {{enSubDone, true}}));
        ASSERT_TRUE(linkExe(enCleanExe, enCleanDone, {{enPostDone, true}, {enAllSubDone, true}}));
        ASSERT_TRUE(linkExe(enSaveExe, enSaveDone, {{enStep_post, true}}));  // dest step_post

        ASSERT_TRUE(okPrev(enSubEnd, {{enSubDone, true}, {enPostDone, true}, {enCleanDone, true}, {enSaveDone, true}}));

        ASSERT_TRUE(okPrev(enAllSubDone, {{enSubDone, true}}));
        ASSERT_TRUE(okPrev(enStep_post, {{enPostDone, true}}));
        ASSERT_TRUE(okPrev(enReportExe, {{enSubEnd, true}}));
        ASSERT_TRUE(okPrev(enPrecheckReport, {{enSubEnd, true}}));
        ASSERT_TRUE(okPrev(enBarAllow, {{enOpen, true}}));
        if (aSub == 0)  // only the first subDest saves for the whole dest, so only it triggers [/dest/logExe]
        {
            ASSERT_TRUE(okPrev(enLogExe, {{enSaveDone, true}}));
        }
    }

    void initFileS(size_t aSub, size_t aFile)
    {
        const auto enJoined        = enFile(FileJoin, aSub, aFile);
        const auto enSignExe       = enFile(FileSigA, aSub, aFile);
        const auto enSumExe        = enFile(FileSigB, aSub, aFile);
        const auto enFileDelivered = enFile(FileDistr, aSub, aFile);
        const auto enFileDone      = enFile(FileDone, aSub, aFile);
        const auto enFailExe       = enFile(FileAbort, aSub, aFile);

        Domino::SimuEvents attrs;
        for (size_t attr = 0; attr < nAttrInFile; ++attr)
        {
            auto enOneAttr = enAttr(aSub, aFile, attr);
            attrs.emplace(enOneAttr, true);
            attrInfoS_.push_back({enOneAttr, enFileDelivered, enFileDone});
            ASSERT_TRUE(okBar(enOneAttr));
        }
        ASSERT_TRUE(okPrev(enJoined, std::move(attrs)));
        ASSERT_TRUE(okPrev(enSignExe, {{enSigSwitch, true}, {enJoined, true}}));
        ASSERT_TRUE(onHdlr(enSignExe));
        ASSERT_TRUE(okPrev(enSumExe, {{enSigSwitch, false}, {enJoined, true}}));
        ASSERT_TRUE(onHdlr(enSumExe));
        ASSERT_TRUE(okPrev(enFileDelivered, {{enSignExe, true}, {enAbortReq, false}}));
        ASSERT_TRUE(okBar(enFileDelivered));

        // No prev, so abort can setState(enFileDone). setPriority before setHdlr.
        ASSERT_TRUE(okPri(enFileDone, EMsgPri_MIN));
        ASSERT_TRUE(onHdlr(enFileDone));

        ASSERT_TRUE(okPri(enFailExe, EMsgPri_HIGH));
        ASSERT_TRUE(okPrev(enFailExe, {{enAbortReq, true}, {enFileDone, false}}));
        ASSERT_TRUE(okHdlr(enFailExe, [this, enFileDone]{
            dom_->setState({{enFileDone, true}});  // not done yet -> close it, so the tail can finish
        }));

        ASSERT_TRUE(okPrev(enFile(FileDown, aSub, aFile), {{enSub(SubPrepDone, aSub), true}}));
        ASSERT_TRUE(onHdlr(enFile(FileDown, aSub, aFile)));

        ASSERT_TRUE(okPrev(enSub(SubAllFileDone, aSub), {{enFileDone, true}}));
        ASSERT_TRUE(okPrev(enSub(SubSumFnc, aSub), {{enFile(FileQuoS[0], aSub, aFile), true}}));
        for (size_t i = 0; i < nFileQuo; ++i)
            ASSERT_TRUE(okPrev(enSub(FileQuoS[i], aSub), {{enFile(FileQuoS[i], aSub, aFile), true}}));
    }

    void initTrueEV()
    {
        Domino::SimuEvents boot{{enSigSwitch, true}, {enBarExe, true}, {enStep_0, true}};
        for (size_t sub = 0; sub < nSub; ++sub)
        {
            boot.emplace(enSub(SubStep_0, sub), true);
            boot.emplace(enSub(SubSigReq, sub), true);
            boot.emplace(enSub(SubPrepReq, sub), true);
            for (size_t file = 0; file < nFile(sub); ++file)
                for (size_t quo = 0; quo < nFileQuoOn; ++quo)
                    boot.emplace(enFile(FileQuoS[quo], sub, file), true);
        }
        ASSERT_EQ(boot.size(), dom_->setState(boot));
    }

    void abortBeforeLastAttr(std::vector<size_t>& aEvOrder)
    {
        const auto firstAbort = std::find(aEvOrder.begin(), aEvOrder.end(), idAbortReq);
        const auto lastAttr = std::find_if(aEvOrder.rbegin(), aEvOrder.rend(),
            [](size_t id) { return id < idAbortReq; }).base() - 1;
        if (firstAbort > lastAttr)
            std::iter_swap(firstAbort, lastAttr);
    }

    void mainUT(bool aNeedAbort)
    {
        // setup dom
        dom_ = std::make_shared<aDom>("load");
        barHdlr_ = [this]
        {
            ++nBar_;
            if (nBar_ == 1) dom_->setState({{enBarChanged, true}});  // one tick is enough to wake barUpdExe
        };
        attrInfoS_.reserve(nEvAttr);
        initDestS();
        if (HasFailure()) return;
        for (size_t sub = 0; sub < nSub; ++sub)
        {
            initSubS(sub);
            if (HasFailure()) return;
            for (size_t file = 0; file < nFile(sub); ++file)
            {
                initFileS(sub, file);
                if (HasFailure()) return;
            }
        }
        initTrueEV();
        if (HasFailure()) return;

        // random events order
        const size_t nAbort = aNeedAbort ? nAbortReq : 0;
        std::vector<size_t> evOrder;
        evOrder.reserve(nEvAttr + nAbort + nMsgPong);
        evOrder.resize(nEvAttr);
        std::iota(evOrder.begin(), evOrder.end(), 0u);
        evOrder.insert(evOrder.end(), nAbort, idAbortReq);   // simulate multiple abortReq events
        evOrder.insert(evOrder.end(), nMsgPong, idMsgPong);  // simulate multiple Msg CB during among events
        std::mt19937 rng(UnitTest::GetInstance()->random_seed());
        std::shuffle(evOrder.begin(), evOrder.end(), rng);  // random order; repeatable with the same seed
        if (aNeedAbort)
            abortBeforeLastAttr(evOrder);

        // occur events by order
        for (auto id : evOrder)
        {
            if (id == idMsgPong)
            {
                drain();
                continue;
            }
            if (id == idAbortReq)
            {
                dom_->setState({{enAbortReq, true}});
                continue;
            }
            const auto& attrInfo = attrInfoS_[id];
            ASSERT_EQ(1u, dom_->setState({{attrInfo.enAttr_, true}}));
            if (dom_->state(attrInfo.enFileDistr_) && !dom_->state(attrInfo.enFileDone_))
            {
                ASSERT_EQ(1u, dom_->setState({{attrInfo.enFileDone_, true}}));
            }
        }
        while (MSG_SELF->nMsg()) drain();
    }

    // Stopped on master succ or master fail. Bar is the progress: succ is 100%, fail is under that.
    void checkEnd(bool aFail)
    {
        EXPECT_NE(aFail, dom_->state(enSucc));
        EXPECT_EQ(aFail, dom_->state(enFail));
        if (aFail)
            EXPECT_LT(nBar_, expectBar());
        else
            EXPECT_EQ(expectBar(), nBar_);
    }

    void dropDom()
    {
        // Queue must already be empty: a queued callback must not run against a destroyed dom.
        ASSERT_EQ(0u, MSG_SELF->nMsg());
        dom_.reset();
        this->pongMsgSelf_();
        EXPECT_EQ(0u, MSG_SELF->nMsg());  // destructor itself enqueues nothing
    }
};
using LoadDom = Types<MinPriDom, MaxNofreeDom, MaxDom>;
TYPED_TEST_SUITE(LoadTest, LoadDom);

// ***********************************************************************************************
TYPED_TEST(LoadTest, success_thenDropDom)
{
    SCOPED_TRACE("seed=" + std::to_string(UnitTest::GetInstance()->random_seed()));
    this->mainUT(false);
    if (this->HasFailure()) return;

    this->checkEnd(false);
    this->dropDom();
}

// ***********************************************************************************************
TYPED_TEST(LoadTest, userAbort_thenDropDom)
{
    SCOPED_TRACE("seed=" + std::to_string(UnitTest::GetInstance()->random_seed()));
    this->mainUT(true);
    if (this->HasFailure()) return;

    this->checkEnd(true);
    this->dropDom();
}

}  // namespace
