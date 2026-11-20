#!/usr/bin/env python3
"""Log 206: time a region candidate inside its program.

The isolated harnesses time a region compiled alone; in 204b/205 that code
was not the code gcc emits for the region in place (a branch alone, a cmov
in the program). This times the candidate where it lives:

  1. splice the candidate into its (preprocessed) translation unit at every
     site of the region id (`c_regions_splice.py splice`);
  2. wrap each function that contains a site with an rdtsc cycle counter
     (the function body is renamed `NAME__timed` and kept inline-able, the
     wrapper keeps the original name);
  3. recompile that one file, link with the program's other objects
     (prebuilt once), and run the program on its recorded-input run;
  4. alternate the original build (the region's own expression spliced the
     same way) and the candidate build; report the ratio of the medians of
     the cycles spent in the wrapped functions.

Output (one line, the CompiledTimer harness format, so `superopt
--program-timer` can read it): `REF CAND SPEEDUP`, REF / CAND being median
cycles of the wrapped functions per run.

    scripts/program_timer.py SPEC.json CANDIDATE.c TRIALS

SPEC.json: {"pp_dir": dir of preprocessed .c files, "regions_json": the
c_regions.py --json of FILE, "file": "deflate.c", "id": region id,
"original": the region's own file, "work": a work dir, "run": ["{exe}",
"args", ...], "cflags": ["-O2", "-w"], "libs": ["-lm"], "max_reps": 5}
"""
import hashlib
import importlib.util
import json
import os
import re
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


c_regions = load("c_regions")
splice_mod = load("c_regions_splice")

CYCLES = "unsigned long long algosearch_cycles;\n"
DUMP_C = r"""
#include <stdio.h>
#include <stdlib.h>
extern unsigned long long algosearch_cycles;
__attribute__((destructor)) static void algosearch_cycles_dump(void) {
    const char *p = getenv("ALGOSEARCH_CYCLES_OUT");
    FILE *f = p ? fopen(p, "w") : NULL;
    if (f) { fprintf(f, "%llu\n", algosearch_cycles); fclose(f); }
}
"""


def wrap_functions(src, names):
    """Rename each function in `names` to NAME__timed and add a cycle-counting
    wrapper with the original name right after it."""
    out, pos = [], 0
    funcs = sorted((f for f in c_regions.functions(src) if f[0] in names), key=lambda f: f[2])
    for name, text, start in funcs:
        brace = text.index("{")
        head = text[:brace]
        m = re.search(r"\b" + re.escape(name) + r"\s*\(", head)
        if not m:
            continue
        params_text = head[m.end():head.rindex(")")]
        params = []
        if params_text.strip() not in ("", "void"):
            for p in params_text.split(","):
                ids = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[.*?\]", "", p))
                params.append(ids[-1])
        ret = head[:m.start()]
        void = re.search(r"\bvoid\b", ret) and "*" not in ret
        inner_head = head[:m.start()] + name + "__timed(" + head[m.end():]
        wrapper_head = re.sub(r"\b(static\s+)?inline\b|__attribute__\s*\(\(\s*always_inline\s*\)\)", "", head)
        call = f"{name}__timed({', '.join(params)})"
        body = ("{ unsigned long long t0_ = __builtin_ia32_rdtsc(); " +
                (f"{call}; algosearch_cycles += __builtin_ia32_rdtsc() - t0_; }}" if void else
                 f"__typeof__({call}) r_ = {call}; algosearch_cycles += __builtin_ia32_rdtsc() - t0_; return r_; }}"))
        out.append(src[pos:start])
        out.append(inner_head + text[brace:] + "\n" + wrapper_head + body + "\n")
        pos = start + len(text)
    out.append(src[pos:])
    return CYCLES + "".join(out)


def build(spec, region_file, tag):
    work = spec["work"]
    os.makedirs(work, exist_ok=True)
    objdir = os.path.join(work, "obj")
    os.makedirs(objdir, exist_ok=True)
    cflags = spec.get("cflags", ["-O2", "-w"])
    others = []
    for fn in sorted(os.listdir(spec["pp_dir"])):
        if not fn.endswith(".c") or fn == spec["file"]:
            continue
        o = os.path.join(objdir, fn[:-2] + ".o")
        if not os.path.exists(o):
            subprocess.run(["gcc", *cflags, "-c", os.path.join(spec["pp_dir"], fn), "-o", o], check=True)
        others.append(o)
    dump = os.path.join(objdir, "algosearch_dump.o")
    if not os.path.exists(dump):
        with open(os.path.join(objdir, "algosearch_dump.c"), "w") as g:
            g.write(DUMP_C)
        subprocess.run(["gcc", "-O2", "-c", os.path.join(objdir, "algosearch_dump.c"), "-o", dump], check=True)
    others.append(dump)
    exe = os.path.join(work, f"exe_{tag}")
    if os.path.exists(exe):
        return exe
    regions = json.load(open(spec["regions_json"]))
    sites = [r for r in regions if r["id"] == spec["id"]]
    funcs = {r["function"] for r in sites}
    src_path = os.path.join(spec["pp_dir"], spec["file"])
    mp = os.path.join(work, f"map_{tag}.txt")
    with open(mp, "w") as g:
        g.write(f"{spec['id']} {region_file}\n")
    spliced = os.path.join(work, f"spliced_{tag}.c")
    with open(os.devnull, "w") as null:
        old = sys.stderr
        sys.stderr = null
        try:
            splice_mod.splice(src_path, spec["regions_json"], mp, spliced)
        finally:
            sys.stderr = old
    text = open(spliced).read()
    # the splice prepended the region function; wrap the enclosing functions
    open(spliced, "w").write(wrap_functions(text, funcs))
    obj = os.path.join(work, f"t_{tag}.o")
    r = subprocess.run(["gcc", *cflags, "-c", spliced, "-o", obj], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("compile failed: " + r.stderr[:500])
    r = subprocess.run(["gcc", "-o", exe, obj, *others, *spec.get("libs", [])], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("link failed: " + r.stderr[:500])
    return exe


def run_cycles(spec, exe):
    with tempfile.NamedTemporaryFile(delete=False) as t:
        path = t.name
    env = dict(os.environ, ALGOSEARCH_CYCLES_OUT=path)
    cmd = [a.replace("{exe}", exe).replace("{work}", spec["work"]) for a in spec["run"]]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=spec["work"])
    try:
        v = int(open(path).read().strip())
    except (OSError, ValueError):
        raise SystemExit("no cycle count (exit %d): %s" % (r.returncode, r.stderr[:300]))
    os.unlink(path)
    if r.returncode:
        raise SystemExit("run failed: " + r.stderr[:300])
    return v


def main():
    spec = json.load(open(sys.argv[1]))
    cand, trials = sys.argv[2], int(sys.argv[3])
    reps = max(3, min(trials, spec.get("max_reps", 5)))
    base = build(spec, spec["original"], "base")
    key = hashlib.sha1(open(cand, "rb").read()).hexdigest()[:16]
    exe = build(spec, cand, "c" + key)
    rb, rc = [], []
    for k in range(reps):
        order = [(base, rb), (exe, rc)] if k % 2 == 0 else [(exe, rc), (base, rb)]
        for e, acc in order:
            acc.append(run_cycles(spec, e))
    if exe != base:
        try:
            os.unlink(exe)
        except OSError:
            pass
    b, c = statistics.median(rb), statistics.median(rc)
    print(f"{b:.0f} {c:.0f} {b / c:.6f}")


if __name__ == "__main__":
    main()
