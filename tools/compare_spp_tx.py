#!/usr/bin/env python3
"""Compare plaintext SPP TX command streams between two logs.

Usage:
  python tools/compare_spp_tx.py --esp esp.log --ref python_gui.log

Expected log lines (either side):
  [TX] Left WRITE_REMOTE_SPEED ...
  [TX]   spp   : 01 9F 05 01 01 30 00 00

The script compares only decoded plaintext SPP bytes, so encryption/wire framing
Differences are ignored and protocol intent can be compared directly.
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys
from typing import Dict, List, Tuple

HEADER_RE = re.compile(r"\[TX\]\s+(Left|Right)\s+([A-Z0-9_]+)")
SPP_RE = re.compile(r"\[TX\]\s+spp\s+:\s+([0-9A-Fa-f ]+)")


def parse_stream(path: pathlib.Path) -> List[Tuple[str, str, str]]:
    seq: List[Tuple[str, str, str]] = []
    wheel = None
    cmd = None

    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        h = HEADER_RE.search(raw)
        if h:
            wheel = h.group(1)
            cmd = h.group(2)
            continue

        s = SPP_RE.search(raw)
        if s and wheel and cmd:
            spp = " ".join(s.group(1).strip().upper().split())
            seq.append((wheel, cmd, spp))
            wheel = None
            cmd = None

    return seq


def summarize(name: str, seq: List[Tuple[str, str, str]]) -> None:
    by_cmd: Dict[str, int] = collections.Counter(cmd for _, cmd, _ in seq)
    by_wheel: Dict[str, int] = collections.Counter(w for w, _, _ in seq)
    print(f"[{name}] total tx frames: {len(seq)}")
    print(f"[{name}] by wheel: {dict(by_wheel)}")
    print(f"[{name}] by cmd  : {dict(by_cmd)}")


def diff_sequences(a_name: str, a: List[Tuple[str, str, str]], b_name: str, b: List[Tuple[str, str, str]], max_show: int) -> None:
    n = max(len(a), len(b))
    shown = 0
    mismatch = 0

    for i in range(n):
        av = a[i] if i < len(a) else None
        bv = b[i] if i < len(b) else None
        if av != bv:
            mismatch += 1
            if shown < max_show:
                print(f"[DIFF {i}] {a_name}: {av}")
                print(f"[DIFF {i}] {b_name}: {bv}")
                shown += 1

    print(f"[COMPARE] mismatched positions: {mismatch} / {n}")


def bag_compare(a_name: str, a: List[Tuple[str, str, str]], b_name: str, b: List[Tuple[str, str, str]], max_show: int) -> None:
    ca = collections.Counter(a)
    cb = collections.Counter(b)

    only_a = ca - cb
    only_b = cb - ca

    print(f"[BAG] unique extra frames in {a_name}: {sum(only_a.values())}")
    print(f"[BAG] unique extra frames in {b_name}: {sum(only_b.values())}")

    shown = 0
    for k, v in only_a.items():
        if shown >= max_show:
            break
        print(f"  {a_name} +{v}: {k}")
        shown += 1
    for k, v in only_b.items():
        if shown >= max_show:
            break
        print(f"  {b_name} +{v}: {k}")
        shown += 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare plaintext SPP TX streams between two logs")
    ap.add_argument("--esp", required=True, help="ESP log path")
    ap.add_argument("--ref", required=True, help="Reference log path (python GUI)")
    ap.add_argument("--max-show", type=int, default=20, help="Max diff examples to print")
    args = ap.parse_args()

    esp_path = pathlib.Path(args.esp)
    ref_path = pathlib.Path(args.ref)

    if not esp_path.exists() or not ref_path.exists():
        print("One or both log files do not exist", file=sys.stderr)
        return 2

    esp = parse_stream(esp_path)
    ref = parse_stream(ref_path)

    summarize("ESP", esp)
    summarize("REF", ref)

    print("\n[MODE] position-by-position compare")
    diff_sequences("ESP", esp, "REF", ref, args.max_show)

    print("\n[MODE] order-independent multiset compare")
    bag_compare("ESP", esp, "REF", ref, args.max_show)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
