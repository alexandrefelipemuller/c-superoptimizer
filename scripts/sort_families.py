#!/usr/bin/env python3
"""Sorting families kept per seed (section 165). Reads RUN_ROOT/seed_N/triage.json.

A program counts if it passes every held-out test (status OK, or ABORT_COST:depth = correct but past the recursion cap).
Families, from the printed text:
  quick     contains call( and the range guard `(v2 - v1) > 1` (recursive partition shell)
  insertion adjacent compare-swap in two nested whiles whose inner counter starts from the outer counter (`vK = vJ;`)
  bubble    same, inner counter restarts from a constant
  exchange  compare-swap of the same pair in two nested whiles, not adjacent
Usage: sort_families.py RUN_ROOT
"""
import glob, json, os, re, sys

sys.path.insert(0, os.path.dirname(__file__))
FAM = ['exchange', 'bubble', 'insertion', 'quick']
CMP = re.compile(r"^(?:if|while) \(\((arr\[.*?\]) (<|>|<=|>=) (arr\[.*?\])\)\) \{$")
SW = re.compile(r"^swap\((arr\[.*?\]), (arr\[.*?\])\);$")

def classify(text):
    out = set()
    if 'call(' in text and '(v2 - v1) > 1' in text:
        out.add('quick')
    lines = [l for l in text.split('\n') if l.strip()]
    stack = []
    for idx, l in enumerate(lines):
        ind = len(l) - len(l.lstrip()); s = l.strip()
        while stack and stack[-1][0] >= ind: stack.pop()
        m = SW.match(s)
        if m:
            loops = [x for x in stack if x[1].startswith('while')]
            if len(loops) >= 2:
                for c in [x[1] for x in stack if x[1].startswith("if")]:
                    cm = CMP.match(c)
                    if cm and {cm.group(1), cm.group(3)} == {m.group(1), m.group(2)}:
                        a, b = [re.sub(r'^arr\[|\]$', '', g) for g in (m.group(1), m.group(2))]
                        adj = (re.fullmatch(re.escape(a) + r' [+-] 1|\(' + re.escape(a) + r' [+-] 1\)', b) or
                               re.fullmatch(re.escape(b) + r' [+-] 1|\(' + re.escape(b) + r' [+-] 1\)', a))
                        if not adj:
                            out.add('exchange')
                        else:
                            # the inner while is the last loop on the stack: find how its counter was initialised
                            inner_i = max(i for i, x in enumerate(stack) if x[1].startswith('while'))
                            inner_line = stack[inner_i][2]
                            cond = re.match(r'while \(\(?(v\d+)', lines[inner_line].strip())
                            kind = 'bubble'
                            if cond:
                                v = cond.group(1)
                                for k in range(inner_line - 1, max(-1, inner_line - 6), -1):
                                    mm = re.match(r'^' + v + r' = (.*);$', lines[k].strip())
                                    if mm:
                                        kind = 'insertion' if re.fullmatch(r'v\d+|\(v\d+ [+-] 1\)', mm.group(1)) else 'bubble'
                                        break
                            out.add(kind)
        if s.endswith('{'): stack.append((ind, s, idx))
    return out

def main():
    root = sys.argv[1]
    per = {f: 0 for f in FAM}; fam_counts = []
    for path in sorted(glob.glob(os.path.join(root, 'seed_*/triage.json')), key=lambda p: int(p.split('seed_')[1].split('/')[0])):
        d = json.load(open(path)); rs = d if isinstance(d, list) else d.get('results', d)
        if isinstance(rs, dict): rs = list(rs.values())
        cnt = {f: 0 for f in FAM}
        for r in rs:
            st = str(r.get('status', ''))
            if not (st == 'OK' or st.startswith('ABORT_COST:depth')): continue
            for f in classify(r.get('program', '')): cnt[f] += 1
        have = [f for f in FAM if cnt[f]]
        for f in have: per[f] += 1
        fam_counts.append(len(have))
        print('%s: %s | families=%d' % (path.split('/')[-2], ' '.join('%s=%d' % (f, cnt[f]) for f in FAM), len(have)))
    print('seeds with family: ' + ' '.join('%s=%d' % (f, per[f]) for f in FAM) + ' | mean families per seed %.2f' % (sum(fam_counts) / max(1, len(fam_counts))))
main()
