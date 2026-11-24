#!/usr/bin/env python3
"""Compare a unary int function against a candidate on all 2^32 arguments.

Log 208: superopt's own exhaustive gate times out when the reference is a
slow loop (popcount over 32 bits: ~80 s for 2^32 calls), so winners on such
samples are reported at level `tested`. This check has no time limit and
splits the range over threads.

    scripts/c_exhaustive_check.py ORIGINAL.c CANDIDATE.c [--func NAME] [--threads 8]

Prints `EXHAUSTIVE_OK name` or `MISMATCH name x ref cand` (first found).
Inputs on which the original's loop never ends are not supported (the
planted set masks its arguments so every input terminates).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HARNESS = r"""
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
int REF(int);
int CAND(int);
static int nthreads;
static volatile int found = 0;
static long long bad = 0;
static void *work(void *arg) {
    long t = (long)arg;
    uint64_t lo = ((uint64_t)1 << 32) * t / nthreads, hi = ((uint64_t)1 << 32) * (t + 1) / nthreads;
    for (uint64_t u = lo; u < hi && !found; ++u) {
        int x = (int)(uint32_t)u;
        if (REF(x) != CAND(x)) { found = 1; bad = x; }
    }
    return 0;
}
int main(int argc, char **argv) {
    nthreads = argc > 1 ? atoi(argv[1]) : 8;
    pthread_t th[64];
    for (long t = 0; t < nthreads; ++t) pthread_create(&th[t], 0, work, (void *)t);
    for (long t = 0; t < nthreads; ++t) pthread_join(th[t], 0);
    if (found) printf("MISMATCH %lld %d %d\n", bad, REF((int)bad), CAND((int)bad));
    else printf("OK\n");
    return 0;
}
"""


def rename(src, name, new):
    return re.sub(r"\b%s\s*\(" % re.escape(name), new + "(", src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("--func", default=None)
    ap.add_argument("--threads", type=int, default=8)
    a = ap.parse_args()
    orig = open(a.original).read()
    cand = open(a.candidate).read()
    name = a.func or re.search(r"\bint\s+(\w+)\s*\(\s*int\s+\w+\s*\)\s*\{", orig).group(1)
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "ref.c"), "w").write(rename(orig, name, "REF"))
        open(os.path.join(d, "cand.c"), "w").write(rename(cand, name, "CAND"))
        open(os.path.join(d, "main.c"), "w").write("#include <stdlib.h>\n" + HARNESS)
        exe = os.path.join(d, "check")
        for f in ("ref", "cand", "main"):
            subprocess.run(["gcc", "-O2", "-fwrapv", "-w", "-c", os.path.join(d, f + ".c"), "-o",
                            os.path.join(d, f + ".o")], check=True)
        subprocess.run(["gcc", "-o", exe] + [os.path.join(d, f + ".o") for f in ("ref", "cand", "main")] +
                       ["-lpthread"], check=True)
        out = subprocess.run([exe, str(a.threads)], capture_output=True, text=True).stdout.strip()
    if out == "OK":
        print("EXHAUSTIVE_OK", name)
    else:
        print(out.replace("MISMATCH", "MISMATCH " + name, 1))
        sys.exit(1)


if __name__ == "__main__":
    main()
