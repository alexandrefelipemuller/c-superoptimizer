#!/usr/bin/env python3
"""Round-trip check of the superoptimizer prototype's C front end and emitter.

For each C file: import it to Mini-C (algosearch c-import), regenerate C
from the Mini-C (--emit), and run the same random + edge-case inputs three
ways: the Mini-C interpreter in wraparound mode, the original C and the
regenerated C, both compiled with gcc -O2 -fwrapv. Inputs on which the
interpreter does not complete (a runtime error = undefined behaviour in C,
or the step limit) are dropped before the native runs and counted.
Every remaining input must give the same result three times, and at least
--min-compared of the inputs must remain.

    scripts/c_roundtrip.py tests/c_samples/*.c [--inputs 2000] [--seed 1]

Exit status 1 if any file fails to import or any result differs.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

INT_MIN, INT_MAX = -2**31, 2**31 - 1
EDGES = [0, 1, -1, 2, -2, 3, 31, 32, 33, 255, 256, -256, 1 << 16, INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1]


def rand_int(rng):
    r = rng.random()
    if r < 0.25:
        return rng.choice(EDGES)
    if r < 0.6:
        return rng.randint(-20, 20)
    if r < 0.8:
        return rng.randint(-5000, 5000)
    return rng.randint(INT_MIN, INT_MAX)


def gen_inputs(abi, params, n, rng):
    lines = []
    for _ in range(n):
        if abi == "scalars":
            vals = [rand_int(rng) for _ in range(params)]
        else:
            length = rng.randint(0, 9)
            small = rng.random() < 0.6
            vals = [rng.randint(-4, 6) if small else rand_int(rng) for _ in range(length)]
        lines.append(" ".join(map(str, vals)))
    return lines


HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
%(protos)s
int main(void) {
    char line[65536];
    while (fgets(line, sizeof line, stdin)) {
        int v[4096], m = 0;
        char *p = line, *end;
        for (;;) {
            long x = strtol(p, &end, 10);
            if (end == p) break;
            v[m++] = (int)x;
            p = end;
        }
%(body)s
    }
    return 0;
}
"""


def harness_source(abi, returns, params):
    if abi == "scalars":
        args = ", ".join(f"int a{i}" for i in range(params))
        call = ", ".join(f"v[{i}]" for i in range(params))
        protos = f"int ref_fn({args});\nint emit_fn({args});"
        body = f'        printf("%d|%d|1\\n", ref_fn({call}), emit_fn({call}));'
        return HARNESS % {"protos": protos, "body": body}
    ret = "int" if returns == "int" else "void"
    protos = f"{ret} ref_fn(int *a, int n);\n{ret} emit_fn(int *a, int n);"
    if returns == "int":
        body = """        int b1[4096], b2[4096];
        memcpy(b1, v, sizeof(int) * m); memcpy(b2, v, sizeof(int) * m);
        int r1 = ref_fn(b1, m), r2 = emit_fn(b2, m);
        /* the array must come back untouched by the regenerated code iff the original leaves it untouched */
        printf("%d|%d|%d\\n", r1, r2, memcmp(b1, b2, sizeof(int) * m) == 0);"""
    else:
        body = """        int b1[4096], b2[4096];
        memcpy(b1, v, sizeof(int) * m); memcpy(b2, v, sizeof(int) * m);
        ref_fn(b1, m); emit_fn(b2, m);
        for (int i = 0; i < m; i++) printf(i ? " %d" : "%d", b1[i]);
        printf("|");
        for (int i = 0; i < m; i++) printf(i ? " %d" : "%d", b2[i]);
        printf("|1\\n");"""
    return HARNESS % {"protos": protos, "body": body}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def check(path, algosearch, workdir, n_inputs, rng, min_compared, native_timeout):
    name = os.path.splitext(os.path.basename(path))[0]
    head = run([algosearch, "c-import", path])
    if head.returncode != 0:
        return False, f"import failed: {head.stderr.strip()}"
    m = re.match(r"// (\w+): abi=(\w+) returns=(\w+) params=(\d+)", head.stdout)
    func, abi, returns, params = m.group(1), m.group(2), m.group(3), int(m.group(4))
    emitted = run([algosearch, "c-import", path, "--emit"])
    if emitted.returncode != 0:
        return False, f"emit failed: {emitted.stderr.strip()}"
    d = os.path.join(workdir, name)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "emitted.c"), "w") as f:
        f.write(emitted.stdout)
    with open(os.path.join(d, "harness.c"), "w") as f:
        f.write(harness_source(abi, returns, params))
    cc = ["gcc", "-O2", "-fwrapv", "-w"]
    for src, obj, new in ((path, "ref.o", "ref_fn"), (os.path.join(d, "emitted.c"), "emit.o", "emit_fn")):
        r = run(cc + [f"-D{func}={new}", "-c", src, "-o", os.path.join(d, obj)])
        if r.returncode != 0:
            return False, f"gcc failed on {src}: {r.stderr.strip()[:400]}"
    r = run(cc + [os.path.join(d, "harness.c"), os.path.join(d, "ref.o"), os.path.join(d, "emit.o"),
                  "-o", os.path.join(d, "harness")])
    if r.returncode != 0:
        return False, f"gcc failed on harness: {r.stderr.strip()[:400]}"

    inputs = gen_inputs(abi, params, n_inputs, rng)
    with open(os.path.join(d, "inputs.txt"), "w") as f:
        f.write("\n".join(inputs) + "\n")
    interp = run([algosearch, "c-import", path, "--eval", os.path.join(d, "inputs.txt")])
    if interp.returncode != 0:
        return False, f"eval failed: {interp.stderr.strip()}"
    results = interp.stdout.splitlines()
    kept = [(i, r) for i, r in zip(inputs, results) if r not in ("ERROR", "TIMEOUT")]
    dropped = {"ERROR": results.count("ERROR"), "TIMEOUT": results.count("TIMEOUT")}
    try:
        native = run([os.path.join(d, "harness")], input="\n".join(i for i, _ in kept) + "\n",
                     timeout=native_timeout)
    except subprocess.TimeoutExpired:
        return False, f"native run exceeded {native_timeout} s (a non-terminating translation?)"
    if native.returncode != 0:
        return False, f"harness crashed (exit {native.returncode})"
    bad = []
    for (inp, mini), line in zip(kept, native.stdout.splitlines()):
        ref, emit, same_array = line.split("|")
        if not (mini == ref == emit and same_array == "1"):
            bad.append(f"input [{inp}]: interpreter={mini} original={ref} regenerated={emit} array_same={same_array}")
    summary = (f"{abi}/{returns}: {len(kept)} compared, dropped {dropped['ERROR']} error "
               f"+ {dropped['TIMEOUT']} timeout")
    if len(native.stdout.splitlines()) != len(kept):
        return False, summary + "; harness output length mismatch"
    if bad:
        return False, summary + f"; {len(bad)} MISMATCHES, first: " + bad[0]
    # A translation that errors on most inputs (e.g. a lost short circuit)
    # would otherwise pass by comparing nothing.
    if len(kept) < min_compared * len(inputs):
        return False, summary + f"; fewer than {min_compared:.0%} of the inputs compared"
    return True, summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--inputs", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--algosearch", default="./algosearch")
    ap.add_argument("--native-timeout", type=float, default=30, help="seconds per file for the compiled runs")
    ap.add_argument("--min-compared", type=float, default=0.5,
                    help="fail a file if fewer than this fraction of its inputs complete in the interpreter")
    ap.add_argument("--workdir", default=None, help="keep build files here (default: a temp dir)")
    args = ap.parse_args()
    workdir = args.workdir or tempfile.mkdtemp(prefix="c_roundtrip_")
    rng = random.Random(args.seed)
    failed = 0
    for path in args.files:
        ok, msg = check(path, args.algosearch, workdir, args.inputs, rng, args.min_compared, args.native_timeout)
        failed += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {os.path.basename(path)}: {msg}")
    print(f"{len(args.files) - failed}/{len(args.files)} passed (build files in {workdir})")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
