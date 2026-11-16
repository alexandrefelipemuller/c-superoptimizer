#!/usr/bin/env python3
"""Log 200: time unary character functions the way real callers use them, in
a loop over a byte buffer (`const char *`), each version inlined into its own
copy of the loop. The in-loop timer and `c_speedup.py --inline` pass each
argument from an int array instead, and for a narrow (char) parameter gcc can
then generate different code for two versions that compile to the same
function (log 200: is_cr_or_lf measured 1.41x there and 1.00x here).

    scripts/c_byte_caller.py INPUTS.txt ORIGINAL.c [CANDIDATE.c ...] [--signed]

INPUTS.txt: one byte value per line (a *_eval_large.txt). The buffer is
`unsigned char` unless --signed. Prints ns per byte (best of 15 x 200
passes) and the speedup over ORIGINAL; the sums of the results must agree.
Compiled with 64-byte function and loop alignment (log 201: without it,
identical copies of one function measured up to 1.27x apart).
"""
import argparse
import os
import re
import subprocess
import tempfile


def renamed(path, func, new):
    text = open(path).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"(?m)^\s*#include[^\n]*\n", "", text)  # the harness includes stdint.h
    text = re.sub(r"\bstatic\s+(inline\s+)?", "", text)
    text = re.sub(r"\binline\s+", "", text)
    return "static inline " + re.sub(rf"\b{func}\b", new, text).strip() + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--signed", action="store_true")
    ap.add_argument("--algosearch", default="./algosearch")
    args = ap.parse_args()
    head = subprocess.run([args.algosearch, "c-import", args.files[0]], capture_output=True, text=True).stdout
    func = re.match(r"// (\w+):", head).group(1)
    ctype = "char" if args.signed else "unsigned char"
    src = "#include <stdio.h>\n#include <stdint.h>\n#include <time.h>\n"
    names = []
    for k, f in enumerate(args.files):
        n = f"f{k}"
        names.append(n)
        src += renamed(f, func, n)
        src += (f"__attribute__((noinline)) long b{k}(const {ctype} *s, int n) "
                f"{{ long c = 0; for (int i = 0; i < n; i++) c += {n}(s[i]); return c; }}\n")
    m = len(args.files)
    src += f"""
static double now(void) {{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }}
int main(int argc, char **argv) {{
    static {ctype} buf[1 << 20]; int n = 0, v; FILE *f = fopen(argv[1], "r");
    while (n < (1 << 20) && fscanf(f, "%d", &v) == 1) buf[n++] = ({ctype})v;
    long (*fs[{m}])(const {ctype} *, int) = {{{", ".join(f"b{k}" for k in range(m))}}};
    double best[{m}]; long chk[{m}];
    for (int k = 0; k < {m}; k++) best[k] = 1e30;
    for (int t = 0; t < 15; t++)
        for (int k = 0; k < {m}; k++) {{
            double a = now(); long c = 0;
            for (int r = 0; r < 200; r++) c += fs[k](buf, n);
            double d = now() - a; if (d < best[k]) best[k] = d; chk[k] = c;
        }}
    for (int k = 0; k < {m}; k++)
        printf("%d %.3f %.3f %ld\\n", k, best[k] / (200.0 * n) * 1e9, best[0] / best[k], chk[k]);
    return 0;
}}
"""
    d = tempfile.mkdtemp(prefix="c_byte_caller_")
    with open(os.path.join(d, "b.c"), "w") as g:
        g.write(src)
    r = subprocess.run(["gcc", "-O2", "-fwrapv", "-w", "-falign-functions=64", "-falign-loops=64", os.path.join(d, "b.c"), "-o", os.path.join(d, "b")],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr[:2000])
    out = subprocess.run([os.path.join(d, "b"), args.inputs], capture_output=True, text=True).stdout.split("\n")
    sums = set()
    for line, f in zip(out, args.files):
        k, ns, sp, chk = line.split()
        sums.add(chk)
        print(f"{os.path.basename(f)}\tns_per_byte {ns}\tspeedup {sp}")
    print("results agree" if len(sums) == 1 else "RESULTS DIFFER")


if __name__ == "__main__":
    main()
