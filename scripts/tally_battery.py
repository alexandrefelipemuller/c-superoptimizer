#!/usr/bin/env python3
"""Solved runs per problem and arm of a joint battery (logs log_ARM_PROBLEM_SEED.txt): a run counts if the N = max-n block reports held-out k/k.
Reproduces section 150b within 1 per arm (761/757 vs 760/756, same per-problem differences). Usage: tally_battery.py ROOT"""
import re,sys,glob,os,collections
root=sys.argv[1]
maxn={}
src=open(''+os.path.join(os.path.dirname(os.path.abspath(__file__)),'run_digit_pieces_joint_battery.sh')+'').read()
for m in re.finditer(r'(\w+):(\d+):(\S+):(\d+)',src.split('LIST="')[1].split('"')[0]): maxn[m.group(1)]=int(m.group(2))
res=collections.defaultdict(lambda: collections.defaultdict(int))
for f in glob.glob(root+'/log_*_*.txt'):
    b=os.path.basename(f)[4:-4]; arm,rest=b.split('_',1); prob,seed=rest.rsplit('_',1)
    t=open(f,errors='replace').read()
    m=re.search(r'^N=%d$'%maxn[prob],t,re.M)
    ok=False
    if m:
        h=re.search(r'held-out (\d+)/(\d+)',t[m.start():m.start()+600])
        ok = bool(h) and h.group(1)==h.group(2)
    res[prob][arm]+=ok
for arm in ('base','PIECE'): pass
tot={'base':0,'PIECE':0}; rows=[]
for p in sorted(res):
    for a in tot: tot[a]+=res[p][a]
    rows.append((p,res[p]['base'],res[p]['PIECE']))
print('TOTAL',tot)
for p,b,c in rows:
    if b!=c: print(p,b,'->',c)
