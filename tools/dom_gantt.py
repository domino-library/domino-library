#!/usr/bin/env python3
# Copyright 2026 Nokia
# Copyright 2026 Shi-Zhong Chen
# Licensed under the BSD 3 Clause license
# SPDX-License-Identifier: BSD-3-Clause
"""
REQ: Domino log -> tab-separated .csv -> Excel stacked-bar Gantt
VALUE: visualize events from Domino log

Input:  Domino log with '(Domino) evName=T/F' and optionally 'parent -T-> child'
Output: build/gantt.csv  (tab-separated, BOM UTF-8, sorted by start)
    Columns:
    timestamp   - first event appears in log; or back to same state for n-go
    en          - "N) EvName#M": N - 1-based id (unchange after sort); M - n-go suffix
    start(s)    - seconds since earliest event
    duration(s) - last occurrence minus first =T
    prev        - direct parent(s) as "N) name", comma-joined
    self_nexts  - self + transitive descendants as N|N|N (pipe-separated ids)
                . Excel cell limit: 32767 chars; ~4 digits/id -> safe up to ~6500 descendants
                . beyond that self_nexts is truncated with trailing '..'; filter still partial-works

Excel (one-time setup, then Refresh All forever):
    1) Data > Get Data > From Text/CSV > select gantt.csv
       Delimiter = Tab > Load -> creates a Table
    2) Select columns C:D > Insert > Bar Chart > Stacked Bar
    3) Right-click "start(s)" series > Format > Fill = No Fill
    4) Save As .xlsx -> template with chart

    Reload: right-click table > Refresh  (or Data > Refresh All = Ctrl+Alt+F5)
       . the Table keeps a binding to gantt.csv path; Refresh re-reads the file
       . if gantt.csv moved/renamed: Data > Queries & Connections > right-click > Edit
         > Source > change path > Close & Load
    Filter: can filter excel -> chart auto-follows the sub-set of events
    Ancestors-of-X: read X's leading N from en column (e.g. "47) myEv"), then AutoFilter
                    self_nexts > Text Filters > Contains > |47|
                    -> shows X + every event that leads to X.
                    Pipe separators prevent false matches (|47| won't hit |147|).
"""
import argparse, re, sys
from pathlib import Path

# -- RegExps -------------------------------------------------------------------------------------
_RE_TS    = re.compile(r'(\d{2}):(\d{2}):(\d{2})[.,](\d+)')
_RE_LINK  = re.compile(r'\(Domino\) (.+) -[TF]-> (.+)')
_RE_STATE = re.compile(r'\(Domino\) (.+)=(T|F)')

def _timestamp(line):
    """Extract first HH:MM:SS.fff -> (raw_string, float_seconds), or None."""
    hit = _RE_TS.search(line)
    if not hit:
        return None
    hour, minute, second, frac = hit[1], hit[2], hit[3], hit[4]
    sec = int(hour) * 3600 + int(minute) * 60 + int(second) + int(frac) / 10 ** len(frac)
    return hit[0], sec

# -- parse: scan log -> timing + prev links ------------------------------------------------------
def parse(log_path):
    """Return (en_1stSec_S, en_lastSec_S, en_1stTsStr_S, en_pre_S).
    n-go: initial -> changed = 1-go; back to initial -> changed = 2-go, ...
    """
    en_1stSec_S, en_lastSec_S, en_1stTsStr_S = {}, {}, {}
    en_pre_S = {}        # en -> [parent events]
    ended_S = set()      # events back to initial; next change starts new run
    en_nRun_S = {}       # en -> current run number (1-based)
    en_enN_S = {}        # en -> en with #N suffix if n-go
    en_initState_S = {}  # en -> init state=T/F

    with open(log_path, errors='replace') as log_file:
        for line in log_file:
            if '(Domino)' not in line:
                continue

            # link: parent -T/F-> child
            hit = _RE_LINK.search(line)
            if hit:
                parent = en_enN_S.get(hit[1], hit[1])
                child = en_enN_S.get(hit[2], hit[2])
                if parent not in en_pre_S.setdefault(child, []):
                    en_pre_S[child].append(parent)
                continue

            # state change: en=T or en=F
            hit = _RE_STATE.search(line)
            if not hit:
                continue
            en = hit[1]
            ts = _timestamp(line)
            if ts is None:
                continue
            tsStr, tsSec = ts

            # remember which direction the first change goes
            if en not in en_initState_S:
                en_initState_S[en] = hit[2]
            # same direction as first observation = changed; opposite = back to initial
            is_changed = (hit[2] == en_initState_S[en])

            # n-go: back to initial then changed again -> new run
            if is_changed and en in ended_S:
                en_nRun_S[en] = en_nRun_S.get(en, 1) + 1
                ended_S.discard(en)

            nRun = en_nRun_S.get(en, 1)
            enN = en if nRun == 1 else '{}#{}'.format(en, nRun)
            en_enN_S[en] = enN

            if enN not in en_1stSec_S:
                en_1stSec_S[enN] = tsSec
                en_1stTsStr_S[enN] = tsStr
            en_lastSec_S[enN] = tsSec

            if not is_changed:
                ended_S.add(en)

    # n-go: propagate prev to later runs (A#2 inherits A's parents)
    for enN in list(en_1stSec_S):
        if enN not in en_pre_S and '#' in enN:
            base_en, suf = enN.rsplit('#', 1)
            if base_en in en_pre_S:
                en_pre_S[enN] = [
                    '{}#{}'.format(p, suf) if '{}#{}'.format(p, suf) in en_1stSec_S else p
                    for p in en_pre_S[base_en]]

    return en_1stSec_S, en_lastSec_S, en_1stTsStr_S, en_pre_S


# -- main ----------------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', help='Domino log file')
    parser.add_argument('-o', '--output', default='build/gantt.csv')
    args = parser.parse_args()

    en_1stSec_S, en_lastSec_S, en_1stTsStr_S, en_pre_S = parse(args.log)
    if not en_1stSec_S:
        print('No (Domino) entries found.', file=sys.stderr)
        sys.exit(1)

    t0 = min(en_1stSec_S.values())
    rows = []
    for en in en_1stSec_S:
        start = en_1stSec_S[en] - t0
        dur = max(en_lastSec_S[en] - en_1stSec_S[en], 0)
        rows.append((en, en_1stTsStr_S[en], start, dur))
    rows.sort(key=lambda r: r[2])

    # stable 1-based id per en, assigned after sort -> survives Excel sort/filter
    en_id_S = {en: i + 1 for i, (en, *_) in enumerate(rows)}

    # forward adjacency (child list) for transitive descendants
    en_next_S = {en: [] for en in en_id_S}
    for child, parents in en_pre_S.items():
        for p in parents:
            if p in en_next_S and child in en_next_S:
                en_next_S[p].append(child)

    def descendants_incl_self(root):
        seen, stack = {root}, [root]
        while stack:
            cur = stack.pop()
            for nxt in en_next_S[cur]:
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        return seen

    lines = ['timestamp\ten\tstart(s)\tduration(s)\tprev\tself_nexts\r\n']
    for en, firstTsStr, start, dur in rows:
        labeled_en = '{}) {}'.format(en_id_S[en], en)
        prev_labeled = ','.join(
            '{}) {}'.format(en_id_S[p], p)
            for p in en_pre_S.get(en, []) if p in en_id_S)
        nexts_ids = sorted(en_id_S[d] for d in descendants_incl_self(en))
        nexts_str = '|'.join(str(i) for i in nexts_ids)
        if len(nexts_str) > 32767:
            nexts_str = nexts_str[:32765] + '..'
        lines.append('{}\t{}\t{:.6f}\t{:.6f}\t{}\t{}\r\n'.format(
            firstTsStr, labeled_en, start, dur, prev_labeled, nexts_str))

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(''.join(lines), encoding='utf-8-sig')
    print('{} <- {} : {} events'.format(args.output, args.log, len(rows)))


if __name__ == '__main__':
    main()

# ------------------------------------------------------------------------------------------------
# 026-04-17  CSZ  1)init