#!/bin/bash
# Copyright 2026 Shi-Zhong Chen
# Licensed under the BSD 3 Clause license
# SPDX-License-Identifier: BSD-3-Clause
#
# REQ:
# - soak & ut: same ut_exe
# - reuse gtest flag: eg --gtest_shuffle, --gtest_repeat=-1, --gtest_random_seed
# - soak with SmartLog (cmake -S .. -B . -DCMAKE_CXX_FLAGS=-DSMART_LOG)
# - enough debug info when soak failed
# - replay: SEED=<seed0> bash ut/soak.sh --gtest_repeat=<it+1>   (printed on fail)
# - nice -n 19 = lowest CPU priority
# - can parallel run
# - $@: extra args override gtest flags (last wins)
# - same cases for ut & soak
# - no soak:
#   . ObjAnywhere, DataStore, MsgSelf: covered by dom* (real)
#   . SafePtr, MT_Notifier: no process-lifetime object of their own
#   . PARA dom w/o rmEvOK: make no sense in soad
#   . ThPoolBack.performance: low ROI
#   . GOLD_entryFn_notify_insteadof_timeout: GTEST_SKIP in case (wall-clock vs nice)

SEED="${SEED:-$(( ($(date +%s) + $$) % 99999 + 1 ))}"
echo "initial_seed=${SEED}"
SOAK=1 TRACE_OFF=1 MALLOC_ARENA_MAX=1 nice -n 19 ./ut_exe --gtest_shuffle --gtest_repeat=-1 --gtest_brief=1 \
  --gtest_random_seed="${SEED}" \
  --gtest_filter='PARA/*:MtInQueue*:ThPoolBackTest.*:AsyncBackTest.*:-ThPoolBackTest.performance' "$@"
