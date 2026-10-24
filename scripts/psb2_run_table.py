#!/usr/bin/env python3
"""Per-run table for a run_psb2.sh output directory. Usage: scripts/psb2_run_table.py run_dir [out.csv]
Columns: problem, seed, solved (JSON records 2,000 unseen cases passed), seconds, scored (candidates scored), executions
(scored x 200 training cases), stop (solved / wall / executions / exhausted / missing)."""
import csv, glob, os, re, sys

D = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else None
TRAIN = 200
rows = []
for log in sorted(glob.glob(f"{D}/psb[12]_*/seed_*.log")):
    problem = os.path.basename(os.path.dirname(log))[5:].removesuffix("_heldout")
    seed = int(re.search(r"seed_(\d+)\.log", log).group(1))
    text = open(log, errors="ignore").read()
    t = re.search(r"Search finished in ([0-9.]+)s", text)
    sc = re.search(r"scored (\d+) \(", text)
    try:
        solved = '"adversarial_passed": 2000' in open(log[:-4] + ".json").read()
    except OSError:
        solved = False
    if "execution budget" in text:
        stop = "executions"
    elif "wall budget" in text:
        stop = "wall"
    elif solved:
        stop = "solved"
    elif t:
        stop = "exhausted"
    else:
        stop = "missing"
    scored = int(sc.group(1)) if sc else 0
    rows.append([problem, seed, int(solved), t.group(1) if t else "", scored, scored * TRAIN, stop])
w = csv.writer(open(OUT, "w", newline="") if OUT else sys.stdout)
w.writerow(["problem", "seed", "solved", "seconds", "scored", "executions", "stop"])
w.writerows(rows)
