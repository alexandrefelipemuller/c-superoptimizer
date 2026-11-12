#!/usr/bin/env python3
"""Sorting family of the printed fastest solution in each benchmark log (section 181). Usage: classify_logs.py DIR [PREFIX]
Reads DIR/log_*.txt, takes the text after "=== fastest solution's program ===", classifies it with sort_families.classify
and prints per problem the solved-with-program count and the families seen. A program with no sorting family is 'none'."""
import glob, os, re, sys
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sort_families.py')).read().split('def main')[0]
ns = {'__file__': os.path.abspath(__file__)}
exec(src, ns)
classify = ns['classify']
d = sys.argv[1]
res = {}
for f in sorted(glob.glob(os.path.join(d, 'log_*.txt'))):
    m = re.match(r'log_(\w+?)_(min_gap|max_gap_sorted)_(\d+)\.txt', os.path.basename(f))
    if not m: continue
    t = open(f).read()
    if "=== fastest solution's program ===" not in t: continue
    prog = t.split("=== fastest solution's program ===")[1]
    prog = '\n'.join(l for l in prog.split('\n') if l.strip() and not l.startswith('[') )
    fams = sorted(classify(prog)) or ['none']
    res.setdefault((m.group(1), m.group(2)), []).append(fams)
for (arm, prob), L in sorted(res.items()):
    cnt = {}
    for fams in L:
        for x in fams: cnt[x] = cnt.get(x, 0) + 1
    print('%s %s programs=%d families: %s' % (arm, prob, len(L), ' '.join('%s=%d' % kv for kv in sorted(cnt.items()))))
