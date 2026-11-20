#!/usr/bin/env python3
"""Log 207: compose region winners greedily, timing each set in its program.

In 206 each region's winner was timed with its neighbours left as originals.
Three Paeth winners that gained alone made their function 0.57x together
(gcc stopped if-converting it). A set of replacements is a new candidate,
so this script times sets:

  current = every region wrapped in its original (the "wrap" build);
  for each candidate (in the given order):
      trial = current with that region replaced by the candidate;
      time trial against current in the program (cycles in the chosen
      functions, alternating runs);
      accept if the median ratio >= THRESHOLD and the trial is faster in
      at least MIN_WINS of the paired runs: current = trial.
  The decision uses the cycles of the timed functions of the file that holds
  the region: a splice recompiles only that file, and the other files' time
  (decoding, compression) would dilute a gain below the noise. Each file has
  its own counter; steps.tsv also gives the total.
  finally time the accepted set against the wrap build.

    scripts/compose_regions.py SPEC.json CANDS.txt OUTDIR

SPEC.json: {"pp_dir": preprocessed .c files, "rj_dir": c_regions.py --json
per file (NAME.json for NAME.c), "files": the files holding region sites,
"wrap": {"file.c": [functions to time]} (outermost only: a function and one
it calls must not both be listed, or cycles count twice), "originals":
{"id": original region file}, "run": ["{exe}", ...], "cflags": [...],
"file_cflags": {"file.c": [extra flags]}, "libs": [...], "reps": 11,
"threshold": 1.02, "min_wins": 9}
CANDS.txt: lines "<id> <candidate.c>", tried in order. A later candidate for
an id already replaced competes against the accepted one.

Writes OUTDIR/steps.tsv, OUTDIR/final_map.txt, OUTDIR/wrap_map.txt.
"""
import hashlib
import importlib.util
import json
import os
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


pt = load("program_timer")

DUMP_C = r"""
#include <stdio.h>
#include <stdlib.h>
%(defs)s
__attribute__((destructor)) static void algosearch_cycles_dump(void) {
    const char *p = getenv("ALGOSEARCH_CYCLES_OUT");
    FILE *f = p ? fopen(p, "w") : NULL;
    if (f) { fprintf(f, "%(fmt)s\n", %(args)s); fclose(f); }
}
"""
splice_mod = load("c_regions_splice")


class Builder:
    def __init__(self, spec, out):
        self.spec, self.out = spec, out
        self.objdir = os.path.join(out, "obj")
        os.makedirs(self.objdir, exist_ok=True)
        self.others = []
        for fn in sorted(os.listdir(spec["pp_dir"])):
            if not fn.endswith(".c") or fn in spec["files"]:
                continue
            o = os.path.join(self.objdir, fn[:-2] + ".o")
            if not os.path.exists(o):
                subprocess.run(["gcc", *self.flags(fn), "-c", os.path.join(spec["pp_dir"], fn), "-o", o], check=True)
            self.others.append(o)
        dump = os.path.join(self.objdir, "algosearch_dump.o")
        if not os.path.exists(dump):
            nf = len(spec["files"])
            with open(os.path.join(self.objdir, "algosearch_dump.c"), "w") as g:
                g.write(DUMP_C % {"defs": "".join(f"unsigned long long algosearch_cycles_{i};\n" for i in range(nf)),
                                  "fmt": " ".join(["%llu"] * nf),
                                  "args": ", ".join(f"algosearch_cycles_{i}" for i in range(nf))})
            subprocess.run(["gcc", "-O2", "-c", os.path.join(self.objdir, "algosearch_dump.c"), "-o", dump], check=True)
        self.others.append(dump)

    def flags(self, fn):
        return self.spec.get("cflags", ["-O2", "-w"]) + self.spec.get("file_cflags", {}).get(fn, [])

    def build(self, mapping):
        text = "".join(f"{k} {mapping[k]}\n" for k in sorted(mapping))
        key = hashlib.sha1((text + "".join(open(mapping[k]).read() for k in sorted(mapping))).encode()).hexdigest()[:16]
        exe = os.path.join(self.out, "exe", key)
        if os.path.exists(exe):
            return exe
        d = os.path.join(self.out, "src", key)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "map.txt"), "w") as g:
            g.write(text)
        objs = []
        for n, fn in enumerate(self.spec["files"]):
            rj = os.path.join(self.spec["rj_dir"], fn[:-2] + ".json")
            src = os.path.join(d, fn)
            old = sys.stderr
            sys.stderr = open(os.devnull, "w")
            try:
                splice_mod.splice(os.path.join(self.spec["pp_dir"], fn), rj, os.path.join(d, "map.txt"), src)
            finally:
                sys.stderr.close()
                sys.stderr = old
            t = pt.wrap_functions(open(src).read(), set(self.spec["wrap"].get(fn, [])))
            t = t.replace(pt.CYCLES, f"extern unsigned long long algosearch_cycles_{n};\n", 1)
            t = t.replace("algosearch_cycles +=", f"algosearch_cycles_{n} +=")
            open(src, "w").write(t)
            o = os.path.join(d, fn[:-2] + ".o")
            r = subprocess.run(["gcc", *self.flags(fn), "-c", src, "-o", o], capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(f"compile failed ({fn}): " + r.stderr[:500])
            objs.append(o)
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        r = subprocess.run(["gcc", "-o", exe, *objs, *self.others, *self.spec.get("libs", [])],
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("link failed: " + r.stderr[:500])
        return exe


def run_cycles(spec, work, exe):
    with tempfile.NamedTemporaryFile(delete=False) as t:
        path = t.name
    env = dict(os.environ, ALGOSEARCH_CYCLES_OUT=path)
    cmd = [a.replace("{exe}", exe).replace("{work}", work) for a in spec["run"]]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=work)
    if r.returncode:
        raise SystemExit("run failed: " + r.stderr[:300])
    v = [int(x) for x in open(path).read().split()]
    os.unlink(path)
    return v


def compare(spec, work, a, b, reps, idx):
    """Alternate a and b. Counter idx (None: the sum of all counters) decides;
    return (median a, median b, pairs where b < a, total a, total b)."""
    ra, rb = [], []
    for k in range(reps):
        for e, acc in ([(a, ra), (b, rb)] if k % 2 == 0 else [(b, rb), (a, ra)]):
            acc.append(run_cycles(spec, work, e))
    pick = (lambda v: sum(v)) if idx is None else (lambda v: v[idx])
    xa, xb = [pick(v) for v in ra], [pick(v) for v in rb]
    wins = sum(1 for x, y in zip(xa, xb) if y < x)
    return (statistics.median(xa), statistics.median(xb), wins,
            statistics.median(sum(v) for v in ra), statistics.median(sum(v) for v in rb))


def main():
    spec = json.load(open(sys.argv[1]))
    cands = [l.split() for l in open(sys.argv[2]) if l.strip()]
    out = os.path.abspath(sys.argv[3])
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, "work")
    os.makedirs(work, exist_ok=True)
    reps, thr, min_wins = spec.get("reps", 11), spec.get("threshold", 1.02), spec.get("min_wins", 9)
    b = Builder(spec, out)
    wrap = dict(spec["originals"])
    with open(os.path.join(out, "wrap_map.txt"), "w") as g:
        g.writelines(f"{k} {v}\n" for k, v in sorted(wrap.items()))
    cur = dict(wrap)
    cur_exe = b.build(cur)
    steps = open(os.path.join(out, "steps.tsv"), "w")
    home = {}
    for i, fn in enumerate(spec["files"]):
        for r in json.load(open(os.path.join(spec["rj_dir"], fn[:-2] + ".json"))):
            home.setdefault(r["id"], i)
    steps.write("step\tid\tcandidate\tfile\tcur_cycles\ttrial_cycles\tratio\twins\ttotal_ratio\taccepted\n")
    for i, fn in enumerate(spec["files"]):
        ca, cb, w, ta, tb = compare(spec, work, cur_exe, cur_exe, reps, i)
        line = f"self\t-\t-\t{fn}\t{ca:.0f}\t{cb:.0f}\t{ca / cb:.4f}\t{w}/{reps}\t{ta / tb:.4f}\t-"
        steps.write(line + "\n"); steps.flush(); print(line, flush=True)
    for n, (rid, cand) in enumerate(cands, 1):
        trial = dict(cur)
        trial[rid] = cand
        t_exe = b.build(trial)
        i = home[rid]
        ca, cb, w, ta, tb = compare(spec, work, cur_exe, t_exe, reps, i)
        ok = ca / cb >= thr and w >= min_wins
        line = (f"{n}\t{rid}\t{cand}\t{spec['files'][i]}\t{ca:.0f}\t{cb:.0f}\t{ca / cb:.4f}\t{w}/{reps}\t"
                f"{ta / tb:.4f}\t{'yes' if ok else 'no'}")
        steps.write(line + "\n"); steps.flush(); print(line, flush=True)
        if ok:
            cur, cur_exe = trial, t_exe
    wrap_exe = b.build(wrap)
    if cur_exe != wrap_exe:
        ca, cb, w, ta, tb = compare(spec, work, wrap_exe, cur_exe, reps, None)
        line = f"final\t-\t-\tall\t{ca:.0f}\t{cb:.0f}\t{ca / cb:.4f}\t{w}/{reps}\t{ta / tb:.4f}\t-"
        steps.write(line + "\n"); print(line, flush=True)
    steps.close()
    with open(os.path.join(out, "final_map.txt"), "w") as g:
        g.writelines(f"{k} {v}\n" for k, v in sorted(cur.items()))
    print("accepted:", sum(1 for k in cur if cur[k] != wrap[k]), "final exe:", cur_exe)


if __name__ == "__main__":
    main()
