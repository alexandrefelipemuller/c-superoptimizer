#!/usr/bin/env python3
"""Log 200: integer workloads for the real-code samples of
tests/c_samples_real2/, one argument per line (`superopt --workload`,
`c_speedup.py --inputs-file`), in the layout of make_char_workloads.py.

    scripts/make_int_workloads.py OUTDIR              # *_search.txt (seed 1), *_eval.txt (seed 2), 1024 each
    scripts/make_int_workloads.py OUTDIR --large N    # *_eval_large.txt, N inputs, seed 3

nibble: uniform 0..15 (nibble_to_hex_digit's callers pass a 4-bit value);
sizes: allocation sizes, log-uniform over 1..65536 (memsys3Roundup).
"""
import math
import os
import random
import sys

KINDS = {
    "nibble": lambda r: r.randint(0, 15),
    "sizes": lambda r: int(math.exp(r.uniform(0, math.log(65536)))),
}


def write(path, values):
    with open(path, "w") as f:
        f.write("\n".join(map(str, values)) + "\n")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    if len(sys.argv) == 4 and sys.argv[2] == "--large":
        n = int(sys.argv[3])
        for kind, gen in KINDS.items():
            r = random.Random(3)
            write(os.path.join(out, f"{kind}_eval_large.txt"), [gen(r) for _ in range(n)])
        return
    for kind, gen in KINDS.items():
        for part, seed in (("search", 1), ("eval", 2)):
            r = random.Random(seed)
            write(os.path.join(out, f"{kind}_{part}.txt"), [gen(r) for _ in range(1024)])


if __name__ == "__main__":
    main()
