#!/usr/bin/env python3
"""Log 195: character workloads for the ctype-like samples, one argument per
line (`superopt --workload`, `c_speedup.py --inputs-file`). Each kind comes in
two files from the same distribution: `*_search.txt` for the search and
`*_eval.txt`, held out, for the external measurement.

    scripts/make_char_workloads.py OUTDIR
    scripts/make_char_workloads.py OUTDIR --large 65536

--large N (log 198): instead, held-out `*_eval_large.txt` files of N inputs, so
an external measurement does not repeat a short sequence the branch predictor
can learn; text from docs/findings/, hex/b64 from another seed.

text: the bytes (0..255) of two documentation files of this repository;
hex: uniform over 0-9a-fA-F; b64: uniform over crypt's alphabet ./0-9A-Za-z.
"""
import os
import random
import sys

N = 1024


def write(path, values):
    with open(path, "w") as f:
        f.write("\n".join(map(str, values)) + "\n")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    if len(sys.argv) == 4 and sys.argv[2] == "--large":
        n = int(sys.argv[3])
        text = b"".join(open(os.path.join("docs/findings", f), "rb").read()
                        for f in sorted(os.listdir("docs/findings")) if f.endswith(".md"))
        write(os.path.join(out, "text_eval_large.txt"), list(text[:n]))
        for kind, alphabet in (("hex", "0123456789abcdefABCDEF"),
                               ("b64", "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")):
            rng = random.Random(3)
            write(os.path.join(out, f"{kind}_eval_large.txt"), [ord(rng.choice(alphabet)) for _ in range(n)])
        return
    write(os.path.join(out, "text_search.txt"), list(open("README.md", "rb").read()[:N]))
    write(os.path.join(out, "text_eval.txt"), list(open("docs/superopt.md", "rb").read()[:N]))
    alphabets = {"hex": "0123456789abcdefABCDEF",
                 "b64": "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"}
    for kind, alphabet in alphabets.items():
        for part, seed in (("search", 1), ("eval", 2)):
            rng = random.Random(seed)
            write(os.path.join(out, f"{kind}_{part}.txt"), [ord(rng.choice(alphabet)) for _ in range(N)])


if __name__ == "__main__":
    main()
