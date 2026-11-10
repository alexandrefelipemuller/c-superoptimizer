#!/usr/bin/env python3
"""Solved runs per problem and arm of a battery with arbitrary arm names (logs log_ARM_PROBLEM_SEED.txt): a run counts if the
N = MAXN block reports held-out k/k. Usage: tally_arms.py ROOT [MAXN=8]"""
import re, sys, glob, os, collections
root = sys.argv[1]; maxn = int(sys.argv[2]) if len(sys.argv) > 2 else 8
res = collections.defaultdict(lambda: collections.defaultdict(int)); arms = []
for f in sorted(glob.glob(root + '/log_*_*.txt')):
    b = os.path.basename(f)[4:-4]; arm, rest = b.split('_', 1); prob, seed = rest.rsplit('_', 1)
    if arm not in arms: arms.append(arm)
    t = open(f, errors='replace').read(); m = re.search(r'^N=%d$' % maxn, t, re.M); ok = False
    if m:
        h = re.search(r'held-out (\d+)/(\d+)', t[m.start():m.start() + 600]); ok = bool(h) and h.group(1) == h.group(2)
    res[prob][arm] += ok
print('problem', *arms)
for p in sorted(res): print(p, *[res[p][a] for a in arms])
