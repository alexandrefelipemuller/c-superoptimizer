#!/usr/bin/env python3
"""Per seed: archived programs that contain call( and pass every held-out test of triage_sorting.py.

Section 163 correction: triage_sorting reports a correct Lomuto quick sort as ABORT_COST:depth (it passes all tests,
then recurses past the 200-deep cap on the sorted/reversed n=256 cost inputs), so counting only status OK missed it.
Usage: scripts/recursive_kept.py RUN_ROOT   (RUN_ROOT holds seed_N/triage.json)
"""
import glob, json, os, sys

root = sys.argv[1]
seeds_with = 0
for path in sorted(glob.glob(os.path.join(root, 'seed_*/triage.json')), key=lambda p: int(p.split('seed_')[1].split('/')[0])):
    d = json.load(open(path))
    rs = d if isinstance(d, list) else d.get('results', d)
    if isinstance(rs, dict):
        rs = list(rs.values())
    ok = [r for r in rs if r.get('status') == 'OK' and r.get('recursive_calls_rev256', 0) > 0 and r.get('core_exponent', 9) < 1.8]
    cap = [r for r in rs if str(r.get('status', '')).startswith('ABORT_COST:depth') and 'call(' in r.get('program', '')]
    sizes = sorted(r['size'] for r in ok + cap)
    seeds_with += bool(ok or cap)
    print('%s: OK recursive %d, correct but depth-capped %d, smallest %s' % (path.split('/')[-2], len(ok), len(cap), sizes[:1]))
print('seeds with a correct recursive sorter: %d' % seeds_with)
