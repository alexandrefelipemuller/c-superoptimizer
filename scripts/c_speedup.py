#!/usr/bin/env python3
"""Compiled speedup of a candidate C function over the original.

Both are compiled with gcc -O2 -fwrapv in separate translation units and
called through a volatile function pointer (no inlining across them), on
the same inputs as the superoptimizer's cost workload: scalar arguments 30%
in [-64, 64] and 70% over all of int, or arrays of 16-64 values in
[-50, 50], 65,536 scalar or 16,384 array inputs per pass (log 198: a short
repeated set lets the branch predictor learn it). Inputs outside the
reference's domain (the interpreter reports ERROR/TIMEOUT within 1M steps)
are dropped. Calls are timed in interleaved
trials, once per link order (geometric mean; functions and loops aligned to
64 bytes, since code placement alone shifted one loop by ~25%); the time
of an empty function with the same signature (call and,
for arrays, the copy of the input) is subtracted. Results of both functions
are also compared on every input.

    scripts/c_speedup.py ORIGINAL.c CANDIDATE.c [--inputs N] [--trials 15] [--seed 1] [--inline]

--inline: each function is compiled into the timing loop's own translation
unit (one executable each, run interleaved), so gcc may inline it the way a
caller would; for functions below the resolution of a timed call.

Prints one line: name, ns/call of each, speedup = original / candidate.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

INT_MIN, INT_MAX = -2**31, 2**31 - 1

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
%(protos)s
static int noop_fn(%(params)s) { %(noop_body)s }
typedef %(ret)s (*fn_t)(%(params)s);
static volatile fn_t fns[3] = {ref_fn, cand_fn, (fn_t)noop_fn};
static volatile long long sink;
static int nin, len[65536], off[65536], data[4000000], buf[4096];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static long long run(int f, int reps) {
    long long acc = 0;
    fn_t fn = fns[f];
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < nin; i++) {
%(call)s
        }
    return acc;
}
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    int trials = atoi(argv[1]);
    char line[65536];
    int pos = 0;
    while (fgets(line, sizeof line, stdin)) {
        char *p = line, *end;
        off[nin] = pos; len[nin] = 0;
        for (;;) { long x = strtol(p, &end, 10); if (end == p) break; data[pos++] = (int)x; len[nin]++; p = end; }
        nin++;
    }
    /* correctness on every input */
    int mismatches = 0;
    for (int i = 0; i < nin; i++) {
%(check)s
    }
    /* calibrate repetitions to ~20 ms per trial and function */
    int reps = 1;
    for (;;) { double t0 = now(); sink += run(0, reps); if (now() - t0 > 2e7 || reps > 1 << 20) break; reps *= 2; }
    double t[3][64];
    for (int k = 0; k < trials; k++)
        for (int j = 0; j < 3; j++) {
            int f = (j + k) %% 3;
            double t0 = now();
            sink += run(f, reps);
            t[f][k] = (now() - t0) / ((double)reps * nin);
        }
    for (int f = 0; f < 3; f++) qsort(t[f], trials, sizeof(double), cmp);
    printf("%%f %%f %%f %%d\n", t[0][trials / 2], t[1][trials / 2], t[2][trials / 2], mismatches);
    return 0;
}
"""


def harness(abi, returns, params):
    if abi == "scalars":
        ps = ", ".join(f"int a{i}" for i in range(params))
        args = ", ".join(f"data[off[i] + {k}]" for k in range(params))
        return HARNESS % {
            "protos": f"int ref_fn({ps});\nint cand_fn({ps});",
            "params": ps, "ret": "int",
            "noop_body": " ".join(f"(void)a{i};" for i in range(params)) + " return 0;",
            "call": f"            acc += fn({args});",
            "check": f"        if (ref_fn({args}) != cand_fn({args})) mismatches++;",
        }
    ret = "int" if returns == "int" else "void"
    call = "            memcpy(buf, data + off[i], sizeof(int) * len[i]);\n"
    call += "            acc += fn(buf, len[i]);" if returns == "int" else "            fn(buf, len[i]); acc += buf[0];"
    if returns == "int":
        check = """        static int b1[4096], b2[4096];
        memcpy(b1, data + off[i], sizeof(int) * len[i]); memcpy(b2, data + off[i], sizeof(int) * len[i]);
        if (ref_fn(b1, len[i]) != cand_fn(b2, len[i]) || memcmp(b1, b2, sizeof(int) * len[i])) mismatches++;"""
    else:
        check = """        static int b1[4096], b2[4096];
        memcpy(b1, data + off[i], sizeof(int) * len[i]); memcpy(b2, data + off[i], sizeof(int) * len[i]);
        ref_fn(b1, len[i]); cand_fn(b2, len[i]);
        if (memcmp(b1, b2, sizeof(int) * len[i])) mismatches++;"""
    return HARNESS % {
        "protos": f"{ret} ref_fn(int *a, int n);\n{ret} cand_fn(int *a, int n);",
        "params": "int *a, int n", "ret": ret,
        "noop_body": "(void)a; (void)n;" + (" return 0;" if returns == "int" else ""),
        "call": call, "check": check,
    }


INLINE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
%(source)s
static volatile long long sink;
static int nin, len[65536], off[65536], data[4000000], buf[4096];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
__attribute__((noinline)) static long long run(int reps) {
    long long acc = 0;
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < nin; i++) {
%(call)s
        }
    return acc;
}
int main(int argc, char **argv) {
    int reps = atoi(argv[1]);
    char line[65536];
    int pos = 0;
    while (fgets(line, sizeof line, stdin)) {
        char *p = line, *end;
        off[nin] = pos; len[nin] = 0;
        for (;;) { long x = strtol(p, &end, 10); if (end == p) break; data[pos++] = (int)x; len[nin]++; p = end; }
        nin++;
    }
    if (reps == 0) {  /* calibrate to ~20 ms */
        reps = 1;
        for (;;) { double t0 = now(); sink += run(reps); if (now() - t0 > 2e7 || reps > 1 << 22) break; reps *= 2; }
        printf("%%d\n", reps);
        return 0;
    }
    sink += run(reps);  /* warm up */
    double t0 = now();
    sink += run(reps);
    printf("%%f\n", (now() - t0) / ((double)reps * nin));
    return 0;
}
"""


def _call(abi, returns, params):
    if abi == "scalars":
        return "            acc += fn(" + ", ".join(f"data[off[i] + {k}]" for k in range(params)) + ");"
    copy = "            memcpy(buf, data + off[i], sizeof(int) * len[i]);\n"
    if returns == "int":
        return copy + "            acc += fn(buf, len[i]);"
    return copy + "            fn(buf, len[i]); acc += buf[0];"


def noop_source(abi, returns, params):
    if abi == "scalars":
        ps = ", ".join(f"int a{i}" for i in range(params))
        # the sum of the arguments, so the loop is not optimized away
        return f"static int fn({ps}) {{ return " + " + ".join(f"a{i}" for i in range(params)) + "; }"
    if returns == "int":
        return "static int fn(int *a, int n) { return n ? a[0] : 0; }"
    return "static void fn(int *a, int n) { (void)a; (void)n; }"


def measure_inline(func, abi, returns, params, original, candidate, kept, trials, d):
    """Each function compiled into the timing loop's own translation unit, renamed to fn."""
    cc = ["gcc", "-O2", "-fwrapv", "-w", "-falign-functions=64", "-falign-loops=64"]
    sources = [re.sub(rf"\b{func}\b", "fn", open(original).read()),
               re.sub(rf"\b{func}\b", "fn", open(candidate).read()),
               noop_source(abi, returns, params)]
    exes = []
    for name, src in zip(("ref", "cand", "noop"), sources):
        c = os.path.join(d, f"inline_{name}.c")
        with open(c, "w") as f:
            f.write(INLINE % {"source": src, "call": _call(abi, returns, params)})
        exe = os.path.join(d, f"inline_{name}")
        r = run(cc + [c, "-o", exe])
        if r.returncode != 0:
            raise SystemExit(f"gcc failed on {c}: {r.stderr.strip()[:400]}")
        exes.append(exe)
    stdin = "\n".join(kept) + "\n"
    reps = int(run([exes[0], "0"], input=stdin).stdout)
    times = [[], [], []]
    for k in range(trials):
        for j in range(3):
            f = (j + k) % 3
            times[f].append(float(run([exes[f], str(reps)], input=stdin).stdout))
    med = [sorted(t)[len(t) // 2] for t in times]
    return max(med[0] - med[2], 1e-3), max(med[1] - med[2], 1e-3), med[2]


def gen_inputs(abi, params, n, rng):
    lines = []
    for _ in range(n):
        if abi == "scalars":
            vals = [rng.randint(-64, 64) if rng.random() < 0.3 else rng.randint(INT_MIN, INT_MAX) for _ in range(params)]
        else:
            vals = [rng.randint(-50, 50) for _ in range(rng.randint(16, 64))]
        lines.append(" ".join(map(str, vals)))
    return lines



def linkable(src, func, out, new, signature=None):
    """A copy of `src` whose target function is not static, so a harness in
    another translation unit can call it (real code, log 192: static helpers
    such as musl's is_leap). Returns it and the gcc flags that rename the
    target to `new`.

    `signature` = (abi, returns, params) for a function with char/short in its
    signature (log 197): the harness calls it as `int new(int, ...)`, which
    leaves the upper result bits undefined, so the target becomes `new_typed`
    and a wrapper `new` with the int signature calls it with C's conversions."""
    text = open(src).read()
    # `inline` goes too: a plain C99 `inline` definition emits no symbol.
    text = re.sub(rf"\bstatic\s+(?:inline\s+)?((?:(?:unsigned\s+)?int|unsigned|uint32_t|int32_t|void)\s+{func}\s*\()",
                  r"\1", text)
    flags = [f"-D{func}={new}"]
    if signature:
        abi, returns, params = signature
        if abi == "scalars":
            ps = ", ".join(f"int a{i}" for i in range(params))
            text += f"\nint {new}({ps}) {{ return {func}(" + ", ".join(f"a{i}" for i in range(params)) + "); }\n"
        elif returns == "int":
            text += f"\nint {new}(int *a, int n) {{ return {func}(a, n); }}\n"
        else:
            text += f"\nvoid {new}(int *a, int n) {{ {func}(a, n); }}\n"
        flags = [f"-D{func}={new}_typed"]
    with open(out, "w") as f:
        f.write(text)
    return out, flags

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def measure(original, candidate, algosearch="./algosearch", n_inputs=None, trials=15, seed=1, workdir=None,
            inline=False, inputs_file=None):
    head = run([algosearch, "c-import", original])
    if head.returncode != 0:
        raise SystemExit(f"import failed: {head.stderr.strip()}")
    m = re.match(r"// (\w+): abi=(\w+) returns=(\w+) params=(\d+)", head.stdout)
    narrow = " narrow=1" in head.stdout.splitlines()[0]
    func, abi, returns, params = m.group(1), m.group(2), m.group(3), int(m.group(4))
    d = workdir or tempfile.mkdtemp(prefix="c_speedup_")
    os.makedirs(d, exist_ok=True)
    rng = random.Random(seed)
    if n_inputs is None:
        # Log 198: one long pass, so the branch predictor cannot learn the
        # sequence (512 repeated inputs made a 5x regression time as 1.5x).
        n_inputs = 65536 if abi == "scalars" else 16384
    if inputs_file:
        # A given workload (log 194): one input per line, in --eval format.
        inputs = [l.strip() for l in open(inputs_file) if l.strip()]
    else:
        inputs = gen_inputs(abi, params, n_inputs * 2, rng)
    with open(os.path.join(d, "inputs.txt"), "w") as f:
        f.write("\n".join(inputs) + "\n")
    ev = run([algosearch, "c-import", original, "--eval", os.path.join(d, "inputs.txt"), "--step-limit", "1000000"])
    kept = [i for i, r in zip(inputs, ev.stdout.splitlines()) if r not in ("ERROR", "TIMEOUT")]
    if inputs_file and 0 < len(kept) < n_inputs:
        # A short workload is resampled in a random order to the full size:
        # its distribution, not a sequence short enough to learn (log 198).
        kept = [rng.choice(kept) for _ in range(n_inputs)]
    kept = kept[:n_inputs]
    if inline:
        ref_ns, cand_ns, noop = measure_inline(func, abi, returns, params, original, candidate, kept, trials, d)
        return {"func": func, "inputs": len(kept), "ref_ns": ref_ns, "cand_ns": cand_ns, "speedup": ref_ns / cand_ns,
                "mismatches": 0, "overhead_ns": noop, "order_speedups": [ref_ns / cand_ns] * 2}
    with open(os.path.join(d, "harness.c"), "w") as f:
        f.write(harness(abi, returns, params))
    # Code placement alone moved the same loop by ~25% (log 188): align
    # functions and loops, and measure with both link orders.
    cc = ["gcc", "-O2", "-fwrapv", "-w", "-falign-functions=64", "-falign-loops=64"]
    objs = []
    for src, new in ((original, "ref_fn"), (candidate, "cand_fn")):
        obj = os.path.join(d, new + ".o")
        src, flags = linkable(src, func, os.path.join(d, new + "_src.c"), new,
                              (abi, returns, params) if narrow else None)
        r = run(cc + flags + ["-c", src, "-o", obj])
        if r.returncode != 0:
            raise SystemExit(f"gcc failed on {src}: {r.stderr.strip()[:400]}")
        objs.append(obj)
    results = []
    for order in (objs, objs[::-1]):
        exe = os.path.join(d, "bench")
        r = run(cc + [os.path.join(d, "harness.c")] + order + ["-o", exe])
        if r.returncode != 0:
            raise SystemExit(f"gcc failed on harness: {r.stderr.strip()[:400]}")
        out = run([exe, str(trials)], input="\n".join(kept) + "\n", timeout=600)
        t_ref, t_cand, t_noop, mismatches = out.stdout.split()
        t_ref, t_cand, t_noop = float(t_ref), float(t_cand), float(t_noop)
        results.append((max(t_ref - t_noop, 1e-3), max(t_cand - t_noop, 1e-3), t_noop, int(mismatches)))
    net_ref = (results[0][0] * results[1][0]) ** 0.5
    net_cand = (results[0][1] * results[1][1]) ** 0.5
    return {"func": func, "inputs": len(kept), "ref_ns": net_ref, "cand_ns": net_cand,
            "speedup": net_ref / net_cand, "mismatches": max(r[3] for r in results),
            "overhead_ns": results[0][2], "order_speedups": [r[0] / r[1] for r in results]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("--inputs", type=int, default=None,
                    help="inputs per pass (default 65536 for scalars, 16384 for arrays; log 198)")
    ap.add_argument("--trials", type=int, default=15)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--algosearch", default="./algosearch")
    ap.add_argument("--workdir", default=None)
    ap.add_argument("--inputs-file", default=None,
                    help="time on these inputs (one per line, as for c-import --eval) instead of random ones; "
                         "the first --inputs of them on which the original completes are used; a shorter file is "
                         "resampled in a random order to --inputs (log 198)")
    ap.add_argument("--inline", action="store_true",
                    help="compile each function into the timing loop's translation unit (gcc may inline it): "
                         "for functions too small to time through a call; no correctness check in this mode")
    a = ap.parse_args()
    r = measure(a.original, a.candidate, a.algosearch, a.inputs, min(a.trials, 63), a.seed, a.workdir, a.inline,
                inputs_file=a.inputs_file)
    print(f"{r['func']}\tref_ns {r['ref_ns']:.2f}\tcand_ns {r['cand_ns']:.2f}\tspeedup {r['speedup']:.3f}"
          f"\tmismatches {r['mismatches']}\tinputs {r['inputs']}\toverhead_ns {r['overhead_ns']:.2f}"
          f"\tby_order {r['order_speedups'][0]:.3f}/{r['order_speedups'][1]:.3f}")
    sys.exit(1 if r["mismatches"] else 0)


if __name__ == "__main__":
    main()
