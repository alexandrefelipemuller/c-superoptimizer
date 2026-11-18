#!/usr/bin/env python3
"""Log 204b: put scalar regions back into a real C file.

Works on the JSON of `c_regions.py --json` (each region site: file offsets
of the expression, and the source text of each input).

    # record the inputs each chosen site sees (reservoir of 65,536 per id):
    scripts/c_regions_splice.py instrument SRC.c REGIONS.json IDS OUT.c LOGDIR
        -> OUT.c calls algosearch_rlog(); link OUT.c with OUT.c.rlog.c.
           At exit LOGDIR/<id>.txt holds one input per line (a workload).

    # replace each chosen site by a call to a function:
    scripts/c_regions_splice.py splice SRC.c REGIONS.json MAP OUT.c
        MAP: lines "<id> <file.c>", the function `region(...)` to call for
        that id (a superopt winner, or the region's own file for the
        "wrapped original" control). All sites with that id are replaced.

IDS: a file with one region id per line. Sites whose text spans a
preprocessor line, or that overlap another chosen site, are skipped (listed
on stderr).
"""
import json
import re
import sys

RLOG_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RLOG_CAP 65536
#define RLOG_IDS %(n)d
static int rlog_vals[RLOG_IDS][RLOG_CAP][4];
static long long rlog_seen[RLOG_IDS];
static int rlog_arity[RLOG_IDS];
static unsigned long long rlog_rng = 88172645463325252ULL;
static const char *rlog_names[RLOG_IDS] = { %(names)s };
static int rlog_registered;
static void rlog_dump(void) {
    for (int k = 0; k < RLOG_IDS; k++) {
        char path[4096];
        snprintf(path, sizeof path, "%(dir)s/%%s.txt", rlog_names[k]);
        FILE *f = fopen(path, "w");
        if (!f) continue;
        long long n = rlog_seen[k] < RLOG_CAP ? rlog_seen[k] : RLOG_CAP;
        for (long long i = 0; i < n; i++) {
            for (int j = 0; j < rlog_arity[k]; j++) fprintf(f, j ? " %%d" : "%%d", rlog_vals[k][i][j]);
            fprintf(f, "\n");
        }
        fclose(f);
        snprintf(path, sizeof path, "%(dir)s/%%s.count", rlog_names[k]);
        f = fopen(path, "w");
        if (f) { fprintf(f, "%%lld\n", rlog_seen[k]); fclose(f); }
    }
}
void algosearch_rlog(int id, int n, long long a, long long b, long long c, long long d) {
    if (!rlog_registered) { rlog_registered = 1; atexit(rlog_dump); }
    long long s = rlog_seen[id]++;
    long long slot = s;
    if (s >= RLOG_CAP) {
        rlog_rng ^= rlog_rng << 13; rlog_rng ^= rlog_rng >> 7; rlog_rng ^= rlog_rng << 17;
        slot = (long long)(rlog_rng %% (unsigned long long)(s + 1));
        if (slot >= RLOG_CAP) return;
    }
    rlog_arity[id] = n;
    rlog_vals[id][slot][0] = (int)a; rlog_vals[id][slot][1] = (int)b;
    rlog_vals[id][slot][2] = (int)c; rlog_vals[id][slot][3] = (int)d;
}
"""


def chosen_sites(src, regions, ids):
    sites = [r for r in regions if r["id"] in ids]
    sites.sort(key=lambda r: r["start"])
    keep, last_end = [], -1
    for r in sites:
        text = src[r["start"]:r["end"]]
        if re.search(r"\n\s*#", text):
            print(f"skip {r['id']} line {r['line']}: spans a preprocessor line", file=sys.stderr)
            continue
        if r["start"] < last_end:
            print(f"skip {r['id']} line {r['line']}: overlaps another site", file=sys.stderr)
            continue
        keep.append(r)
        last_end = r["end"]
    return keep


def replace_all(src, sites, make):
    out, pos = [], 0
    for r in sites:
        out.append(src[pos:r["start"]])
        out.append(make(r, src[r["start"]:r["end"]]))
        pos = r["end"]
    out.append(src[pos:])
    return "".join(out)


def instrument(src_path, json_path, ids_path, out_path, logdir):
    src = open(src_path).read()
    regions = json.load(open(json_path))
    ids = [l.split()[0] for l in open(ids_path) if l.strip()]
    sites = chosen_sites(src, regions, set(ids))
    index = {k: i for i, k in enumerate(ids)}

    def make(r, text):
        args = [f"(long long)({i['src']})" for i in r["inputs"]] + ["0"] * (4 - len(r["inputs"]))
        return f"(algosearch_rlog({index[r['id']]}, {len(r['inputs'])}, {', '.join(args)}), ({text}))"

    head = "void algosearch_rlog(int, int, long long, long long, long long, long long);\n"
    open(out_path, "w").write(head + replace_all(src, sites, make))
    names = ", ".join(f'"{k}"' for k in ids)
    open(out_path + ".rlog.c", "w").write(RLOG_C % {"n": len(ids), "names": names, "dir": logdir})
    print(f"instrumented {len(sites)} sites of {len(ids)} ids", file=sys.stderr)


def splice(src_path, json_path, map_path, out_path):
    src = open(src_path).read()
    regions = json.load(open(json_path))
    mapping = {}
    for line in open(map_path):
        if line.strip():
            k, f = line.split()
            mapping[k] = f
    sites = chosen_sites(src, regions, set(mapping))
    defs = []
    for n, (k, f) in enumerate(mapping.items()):
        code = open(f).read()
        code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
        code = re.sub(r"(?m)^\s*#include[^\n]*\n", "", code)
        code = re.sub(r"\bstatic\s+(inline\s+)?", "", code)
        code = re.sub(r"\bregion\s*\(", f"algosearch_region_{n}(", code, count=1)
        defs.append("static inline " + code.strip() + "\n")
    names = {k: f"algosearch_region_{n}" for n, k in enumerate(mapping)}

    def make(r, text):
        return f"{names[r['id']]}({', '.join('(' + i['src'] + ')' for i in r['inputs'])})"

    open(out_path, "w").write("".join(defs) + replace_all(src, sites, make))
    print(f"spliced {len(sites)} sites of {len(mapping)} ids", file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "instrument" and len(sys.argv) == 7:
        instrument(*sys.argv[2:])
    elif len(sys.argv) >= 2 and sys.argv[1] == "splice" and len(sys.argv) == 6:
        splice(*sys.argv[2:])
    else:
        sys.exit(__doc__)
