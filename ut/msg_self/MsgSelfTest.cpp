/**
 * Copyright 2016-2022 Nokia
 * Copyright 2026 Shi-Zhong Chen
 * Licensed under the BSD 3 Clause license
 * SPDX-License-Identifier: BSD-3-Clause
 */
// ***********************************************************************************************
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>  // for shared_ptr
#include <queue>

#include "MsgSelf.hpp"
#include "MT_PingMainTH.hpp"

using namespace std;
using namespace std::chrono;
using namespace testing;

namespace rlib
{
// ***********************************************************************************************
struct MsgSelfTest : public Test, public UniLog
{
    MsgSelfTest() : UniLog(UnitTest::GetInstance()->current_test_info()->name()) { mt_getMainTH(); }
    ~MsgSelfTest() { GTEST_LOG_FAIL }

    // -------------------------------------------------------------------------------------------
    shared_ptr<MsgSelf> msgSelf_ = make_shared<MsgSelf>(uniLogName());

    MsgCB d1MsgHdlr_ = [&](){ hdlrIDs_.push(1); };
    MsgCB d2MsgHdlr_ = [&](){ hdlrIDs_.push(2); };
    MsgCB d3MsgHdlr_ = [&](){ hdlrIDs_.push(3); };
    MsgCB d4MsgHdlr_ = [&](){ hdlrIDs_.push(4); };
    MsgCB d5MsgHdlr_ = [&]()
    {
        hdlrIDs_.push(5);
        EXPECT_TRUE(msgSelf_->newMsgOK(d2MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";
    };
    MOCK_METHOD(void, d6MsgHdlr, ());

    std::queue<int> hdlrIDs_;
};

#define SEND_MSG
// ***********************************************************************************************
TEST_F(MsgSelfTest, GOLD_sendMsg)
{
    EXPECT_EQ(0u, msgSelf_->nMsg()) << "REQ: init states";

    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_EQ(1u, msgSelf_->nMsg(EMsgPri_NORM));
    EXPECT_EQ(1u, msgSelf_->nMsg());
    EXPECT_EQ(queue<int>(), hdlrIDs_) << "REQ: not immediate call d1MsgHdlr_ but wait msg-to-self";

    msgSelf_->handleAllMsg();  // simulate main() callback
    EXPECT_EQ(queue<int>({1}), hdlrIDs_) << "REQ: call d1MsgHdlr_";
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_NORM));
    EXPECT_FALSE(msgSelf_->nMsg());
}
TEST_F(MsgSelfTest, dupSendMsg)
{
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";  // dup
    EXPECT_EQ(2u, msgSelf_->nMsg(EMsgPri_NORM));
    EXPECT_EQ(queue<int>(), hdlrIDs_);

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({1, 1}), hdlrIDs_);  // req: call all
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_NORM));
}
TEST_F(MsgSelfTest, GOLD_loopback_handleAll_butOneByOneLowPri)
{
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_LOW)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_LOW)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_LOW)) << "REQ: new msg OK";
    EXPECT_EQ(2u, msgSelf_->nMsg(EMsgPri_NORM));
    EXPECT_EQ(2u, msgSelf_->nMsg(EMsgPri_HIGH));
    EXPECT_EQ(3u, msgSelf_->nMsg(EMsgPri_LOW));

    msgSelf_->handleAllMsg();
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_NORM)) << "REQ: all normal";
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_HIGH)) << "REQ: all high";
    EXPECT_EQ(2u, msgSelf_->nMsg(EMsgPri_LOW))  << "REQ: 1/loopback()";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(1u, msgSelf_->nMsg(EMsgPri_LOW));

    msgSelf_->handleAllMsg();
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_LOW));

    msgSelf_->handleAllMsg();
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_LOW));
}

#define PRI
// ***********************************************************************************************
TEST_F(MsgSelfTest, GOLD_highPriority_first)
{
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d2MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({2, 1}), hdlrIDs_);
}
TEST_F(MsgSelfTest, GOLD_samePriority_fifo)
{
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d2MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d3MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d4MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({2, 4, 1, 3}), hdlrIDs_);
}
TEST_F(MsgSelfTest, newHighPri_first)
{
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d5MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d3MsgHdlr_)) << "REQ: new msg OK";
    EXPECT_TRUE(msgSelf_->newMsgOK(d4MsgHdlr_, EMsgPri_HIGH)) << "REQ: new msg OK";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({5, 4, 2, 1, 3}), hdlrIDs_);
}
TEST_F(MsgSelfTest, newLowPri_inNormHandler_handledSameRound)
{
    // the low-pri yield triggers only AFTER a LOW msg runs; so a LOW created while handling a
    // NORM msg (no LOW run yet this round) shall still run in the SAME handleAllMsg() round
    MsgCB normAddsLow = [&]()
    {
        hdlrIDs_.push(30);
        EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_LOW)) << "REQ: new msg OK";
    };
    EXPECT_TRUE(msgSelf_->newMsgOK(normAddsLow)) << "REQ: new msg OK";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({30, 1}), hdlrIDs_)   << "REQ: low after norm runs same round (no prior low yield)";
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_LOW)) << "REQ: nothing deferred";
}
TEST_F(MsgSelfTest, newLowPri_inLowHandler_deferred_toNextHandleAll)
{
    // low priority shall yield after 1 msg/handleAllMsg, so a LOW msg created inside a LOW
    // handler shall NOT run in the same handleAllMsg() round
    MsgCB lowAddsLow = [&]()
    {
        hdlrIDs_.push(20);
        EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_LOW)) << "REQ: new msg OK";
    };
    EXPECT_TRUE(msgSelf_->newMsgOK(lowAddsLow, EMsgPri_LOW)) << "REQ: new msg OK";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({20}), hdlrIDs_)        << "REQ: low-pri yields after 1, new low deferred";
    EXPECT_EQ(1u, msgSelf_->nMsg(EMsgPri_LOW))   << "REQ: new low still pending";

    msgSelf_->handleAllMsg();
    EXPECT_EQ(queue<int>({20, 1}), hdlrIDs_)     << "REQ: deferred low handled next round";
    EXPECT_EQ(0u, msgSelf_->nMsg(EMsgPri_LOW));
}

#define DESTRUCT_MSGSELF
// ***********************************************************************************************
TEST_F(MsgSelfTest, destructMsgSelf_noCallback_noMemLeak_noCrash)  // mem leak is checked by valgrind upon UT
{
    EXPECT_CALL(*this, d6MsgHdlr()).Times(0);  // REQ: no call
    EXPECT_TRUE(msgSelf_->newMsgOK([&](){ this->d6MsgHdlr(); })) << "REQ: new msg OK";
    EXPECT_EQ(1u, msgSelf_->nMsg(EMsgPri_NORM));

    msgSelf_.reset();  // rm msgSelf
    INF("~MsgSelf() shall print msg discarded");
}

#define WAIT_NOTIFY
// ***********************************************************************************************
TEST_F(MsgSelfTest, wait_notify)
{
    auto start = high_resolution_clock::now();
    EXPECT_TRUE(msgSelf_->newMsgOK(d1MsgHdlr_)) << "REQ: new msg OK";
    timedwait(0, 100);  // REQ: 1 msg will wakeup MT_Notifier::timedwait()
    auto dur = duration_cast<std::chrono::milliseconds>(high_resolution_clock::now() - start);
    EXPECT_LT(dur.count(), 100) << "REQ: newMsgOK() shall notify instead of timeout";

    msgSelf_->handleAllMsg();  // clear msg queue
}

#define SAFE
// ***********************************************************************************************
TEST_F(MsgSelfTest, invalidMsg_noCrash)
{
    EXPECT_FALSE(msgSelf_->newMsgOK(MsgCB())) << "REQ: null msg NOK";
    EXPECT_FALSE(msgSelf_->newMsgOK(d1MsgHdlr_, EMsgPri_MAX)) << "REQ: NOK priority";
    EXPECT_FALSE(msgSelf_->newMsgOK(nullptr, EMsgPriority(-1))) << "REQ: NOK priority";
    msgSelf_->handleAllMsg();  // req: no crash (& inc cov of empty queue)
    EXPECT_EQ(queue<int>(), hdlrIDs_);
    EXPECT_EQ(0, msgSelf_->nMsg());
}
TEST_F(MsgSelfTest, exceptMsg_noCrash)
{
    int step = 0;
    EXPECT_TRUE(msgSelf_->newMsgOK([&step]{ ++step; throw runtime_error("except msg"); }));
    msgSelf_->handleAllMsg();
    EXPECT_EQ(1, step) << "REQ: msg executed";
    EXPECT_EQ(0, msgSelf_->nMsg()) << "REQ: handled & no crash";
}
TEST_F(MsgSelfTest, invalid_nMsg)
{
    EXPECT_EQ(0, msgSelf_->nMsg(EMsgPri_MAX))      << "REQ: not accept out bound priority";
    EXPECT_EQ(0, msgSelf_->nMsg(EMsgPriority(-1))) << "REQ: not accept out bound priority";
}

}  // namespace
