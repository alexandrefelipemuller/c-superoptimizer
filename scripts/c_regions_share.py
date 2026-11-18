#!/usr/bin/env python3
"""Log 205: how much of a program's execution is in extractable scalar regions.

Given preprocessed sources (gcc -E -P, one .c per translation unit) and the
.gcda files of a --coverage build of exactly those files run on the
benchmark, counts executed source lines (gcov) and the lines that hold a
region (c_regions.py on the same files). Prints, per file and in total:

  - line executions: all lines, and lines holding a region;
  - region executions (a line's count, once per region on it), and the
    importable ones with >= 3 operators;
  - the hottest regions.

The line-execution share is a proxy for time share (no perf on this machine).

    scripts/c_regions_share.py --pp PPDIR --cov COVDIR [--json OUT.json] [--top 20]
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys

_spec = importlib.util.spec_from_file_location("c_regions", os.path.join(os.path.dirname(__file__), "c_regions.py"))
c_regions = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c_regions)


def gcov_counts(cov, src):
    """line -> count for src (a file compiled in cov)."""
    base = os.path.basename(src)
    gcda = [f for f in os.listdir(cov) if f.endswith(".gcda") and re.search(r"(^|-)" + re.escape(base[:-2]) + r"\.gcda$", f)]
    if not gcda:
        return {}
    subprocess.run(["gcov", "-o", cov, gcda[0]], cwd=cov, capture_output=True, text=True)
    path = os.path.join(cov, base + ".gcov")
    counts = {}
    if not os.path.exists(path):
        return counts
    for line in open(path, errors="replace"):
        m = re.match(r"\s*([^:]+):\s*(\d+):", line)
        if not m:
            continue
        c = m.group(1).strip().rstrip("*")
        if c.isdigit():
            counts[int(m.group(2))] = int(c)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pp", required=True)
    ap.add_argument("--cov", required=True)
    ap.add_argument("--json", default=None)
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--algosearch", default="./algosearch")
    args = ap.parse_args()
    tot_lines = tot_region_lines = 0
    rows, hot = [], []
    for fn in sorted(os.listdir(args.pp)):
        if not fn.endswith(".c"):
            continue
        src_path = os.path.join(args.pp, fn)
        src = open(src_path, errors="replace").read()
        counts = gcov_counts(args.cov, src_path)
        all_exec = sum(counts.values())
        region_lines = set()
        for name, text, start in c_regions.functions(src):
            base = src.count("\n", 0, start)
            for r, ins, ctx, line, (ca, cb), srcs in c_regions.regions_of(name, text, 3, 4):
                ln = base + line
                c = counts.get(ln, 0)
                region_lines.add(ln)
                code, body, rt = c_regions.emit(r, ins, f"{fn}:{ln} in {name}")
                hot.append({"file": fn, "line": ln, "function": name, "count": c, "ops": r.ops,
                            "text": c_regions.render(r), "start": start + ca, "end": start + cb, "code": code,
                            "inputs": [{"key": k, "src": srcs[k], "type": t[0]} for k, t in ins.items()]})
        reg_exec = sum(counts.get(l, 0) for l in region_lines)
        rows.append((fn, all_exec, reg_exec))
        tot_lines += all_exec
        tot_region_lines += reg_exec
    print("file\tline_executions\ton_region_lines\tshare")
    for fn, a, b in rows:
        if a:
            print(f"{fn}\t{a}\t{b}\t{b / a:.4f}")
    print(f"TOTAL\t{tot_lines}\t{tot_region_lines}\t{tot_region_lines / max(1, tot_lines):.4f}")
    hot.sort(key=lambda h: -h["count"])
    print(f"regions: {len(hot)}, executed: {sum(1 for h in hot if h['count'])}, "
          f"region executions: {sum(h['count'] for h in hot)}")
    for h in hot[:args.top]:
        print(f"{h['count']:>12} {h['file']}:{h['line']} {h['function']}: {h['text'][:80]}")
    if args.json:
        json.dump(hot, open(args.json, "w"), indent=1)


if __name__ == "__main__":
    main()
