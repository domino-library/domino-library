#!/bin/bash
# Copyright 2026 Shi-Zhong Chen
# Licensed under the BSD 3 Clause license
# SPDX-License-Identifier: BSD-3-Clause
#
# REQ:
# - soak & ut: same ut_exe
# - reuse gtest flag: eg --gtest_shuffle, --gtest_repeat=-1, --gtest_random_seed
# - soak with SmartLog (cmake -S .. -B build-smart -Dci=smart)
# - enough debug info when soak failed
# - replay: SEED=<seed0> bash ut/soak.sh --gtest_repeat=<it+1>   (printed on fail)
# - nice -n 19 = lowest CPU priority
# - can parallel run
# - ONE soak per directory (avoid shared soak.log; avoid more logs)
# - $@: extra args override gtest flags (last wins)
# - same cases for ut & soak
# - no soak:
#   . ObjAnywhere, DataStore, MsgSelf: covered by dom* (real)
#   . SafePtr, MT_Notifier: no process-lifetime object of their own
#   . PARA dom w/o rmEvOK: make no sense in soad
#   . ThPoolBack.performance: low ROI
#   . GOLD_entryFn_notify_insteadof_timeout: GTEST_SKIP in case (wall-clock vs nice)
# - asan rss climbs then plateaus (quarantine ~256MB + shadow); not a leak
# - MALLOC_ARENA_MAX=1: glibc arenas only; asan/tsan replace malloc
# - lsan checks at exit; this loop does not, so leaks are make run
# - screen tail survives crash: soak.log + soak.log.1 (~10MB each) in the build dir; usage:
#   tail -n 40 ~/dom/build/soak.log
#   tail -n 40 ~/dom/build-smart/soak.log
#   tail -n 40 ~/dom/build-tsan/soak.log

# tee -p: a dead log reader must not kill the soak.
# awk: local timestamp, rotate at ~10MB, sync the file about once a second (hard reboot).
exec > >(tee -p >(LC_ALL=C awk -v f=soak.log 'BEGIN {
    while ((getline < f) > 0) n += length($0) + 1
    close(f)
} {
    s = strftime("%F %T ") $0
    print s >> f; fflush(f)
    n += length(s) + 1
    if (systime() > t) { t = systime(); system("sync " f) }
    if (n > 1e7) { close(f); system("sync " f); system("mv -f " f " " f ".1"); n = 0 }
}')) 2>&1
SEED="${SEED:-$(( ($(date +%s) + $$) % 99999 + 1 ))}"
echo "initial_seed=${SEED}"
# --gtest_color: gtest drops color when stdout is not a tty.
SOAK=1 TRACE_OFF=1 MALLOC_ARENA_MAX=1 nice -n 19 ./ut_exe \
    --gtest_color=yes --gtest_shuffle --gtest_repeat=-1 --gtest_brief=1 \
    --gtest_random_seed="${SEED}" \
    --gtest_filter='PARA/*:MtInQueue*:ThPoolBackTest.*:AsyncBackTest.*:-ThPoolBackTest.performance' \
    "$@"
