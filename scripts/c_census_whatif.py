#!/usr/bin/env python3
"""Log 210 (step 0): which front-end blocker, if lifted, opens the most real functions?

The census (c_census.py --preprocess) records only the first reason a function
fails; most functions have several. This diagnostic re-imports every
preprocessed unit after rewriting its text to lift one or more blockers, as a
stand-in for front-end support. The rewrites do not preserve semantics (they
are an upper bound on what each feature could open, not a translation):

  S  string literals      "..." (and adjacent concatenations) -> 0
  B  byte/short elements  char, short, int8..16 types -> int (so `uint8_t *p`
                          reads as `int *p`; scalars of those types were already
                          supported, so they are unaffected in practice)
  Z  64-bit integers      size_t, ssize_t, long, long long -> int / unsigned
  L  literal suffixes     1UL, 0xffL -> 1u, 0xff
  P  pointer typedefs     `typedef T *name;` expanded in place (png_bytep -> png_byte *)

    scripts/c_census_whatif.py --out DIR --base NAME=SRC_DIR[:CFLAGS] ...

Writes DIR/<base>.<arm>.tsv (function, status) and DIR/summary.tsv; prints the
table. Arms: none, each letter alone, all, and all-but-each letter.
"""
import argparse
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c_preprocess  # noqa: E402

LETTERS = "SBZLP"


def lift_strings(t):
    return re.sub(r'"(?:\\.|[^"\\\n])*"(?:\s*"(?:\\.|[^"\\\n])*")*', "0", t)


def lift_bytes(t):
    t = re.sub(r"\b(?:signed|unsigned)\s+(?:char|short(?:\s+int)?)\b", "int", t)
    return re.sub(r"\b(?:char|short|u?int(?:8|16)_t|__u?int(?:8|16)_t)\b", "int", t)


def lift_wide(t):
    t = re.sub(r"\bunsigned\s+long(?:\s+long)?(?:\s+int)?\b", "unsigned", t)
    t = re.sub(r"\b(?:signed\s+)?long(?:\s+long)?(?:\s+int)?\b", "int", t)
    t = re.sub(r"\b(?:__)?u?int64_t\b|\b__u_?quad_t\b", "int", t)
    return re.sub(r"\b(?:s?size_t|ptrdiff_t|intptr_t|uintptr_t)\b", "int", t)


def lift_suffix(t):
    def fix(m):
        return m.group(1) + ("u" if "u" in m.group(2).lower() else "")
    return re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)([uU]?[lL]{1,2}|[lL]{1,2}[uU]?)\b", fix, t)


def lift_ptr_typedefs(t):
    for _ in range(4):  # chains: png_const_bytep -> const png_byte * -> ...
        defs = dict((m.group(2), m.group(1).strip() + " *")
                    for m in re.finditer(r"(?m)^\s*typedef\s+([\w\s]+?)\s*\*\s*(\w+)\s*;\s*$", t))
        if not defs:
            break
        t = re.sub(r"(?m)^\s*typedef\s+[\w\s]+?\s*\*\s*\w+\s*;\s*$", "", t)
        t = re.sub(r"\b(%s)\b" % "|".join(map(re.escape, defs)), lambda m: defs[m.group(1)], t)
    return t


LIFT = {"S": lift_strings, "B": lift_bytes, "Z": lift_wide, "L": lift_suffix, "P": lift_ptr_typedefs}


def drop_self_typedefs(t):
    """A lift can turn `typedef __uint8_t uint8_t;` into `typedef int int;`: drop those."""
    return re.sub(r"(?m)^\s*typedef\s+[\w\s]*\b(?:int|unsigned)\s*;\s*$", "", t)


def arms():
    out = [""] + list(LETTERS) + [LETTERS] + [LETTERS.replace(c, "") for c in LETTERS]
    return list(dict.fromkeys(out))


def reason(msg):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import c_census
    return c_census.reason(msg)


def import_one(algosearch, tmp, key, name, text):
    path = os.path.join(tmp, key + ".c")
    with open(path, "w") as g:
        g.write(text)
    r = subprocess.run([algosearch, "c-import", path, "--func", name], capture_output=True, text=True)
    os.unlink(path)
    return "OK" if r.returncode == 0 else reason(r.stderr or r.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--base", action="append", required=True, help="NAME=SRC_DIR[:CFLAGS]")
    ap.add_argument("--algosearch", default="./algosearch")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    table = []
    for spec in a.base:
        name, _, rest = spec.partition("=")
        src, _, extra = rest.partition(":")
        cflags = shlex.split(extra)
        dirs = sorted({r for r, _, ns in os.walk(src) if any(n.endswith(".h") for n in ns)})
        cflags += ["-I" + d for d in dirs]
        units = []
        for root, _, names in os.walk(src):
            for f in sorted(names):
                if f.endswith(".c"):
                    path = os.path.join(root, f)
                    found, err = c_preprocess.units(path, cflags + ["-I" + root])
                    units += [(path, fn, text) for fn, text in found]
        print(f"{name}: {len(units)} preprocessed functions", file=sys.stderr)
        with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            for arm in arms():
                def job(k):
                    path, fn, text = units[k]
                    for c in arm:
                        text = LIFT[c](text)
                    text = drop_self_typedefs(text)
                    return import_one(a.algosearch, tmp, "u%d" % k, fn, text)
                status = list(ex.map(job, range(len(units))))
                with open(os.path.join(a.out, f"{name}.{arm or 'none'}.tsv"), "w") as g:
                    g.write("file\tfunction\tstatus\n")
                    for (path, fn, _), s in zip(units, status):
                        g.write(f"{path}\t{fn}\t{s}\n")
                ok = sum(s == "OK" for s in status)
                table.append((name, arm or "none", ok, len(units)))
                print(f"  {arm or 'none':5s} {ok}", file=sys.stderr)
    with open(os.path.join(a.out, "summary.tsv"), "w") as g:
        g.write("base\tarm\timport\tfunctions\n")
        for row in table:
            g.write("\t".join(map(str, row)) + "\n")
    totals = {}
    for _, arm, ok, n in table:
        t = totals.setdefault(arm, [0, 0])
        t[0] += ok
        t[1] += n
    for arm, (ok, n) in totals.items():
        print(f"{arm:5s} {ok:6d} / {n}  ({100.0 * ok / n:.2f}%)")


if __name__ == "__main__":
    main()
