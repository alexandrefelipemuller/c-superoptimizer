#!/usr/bin/env python3
"""Log 199: string workloads for the hand-ported musl string samples
(tests/c_samples_strings_pilot/), one array per line, a string as one char
(0..255) per element, NUL-terminated (`superopt --workload`,
`c_speedup.py --inputs-file`). `*_search.txt` take their words from README.md,
`*_eval.txt` (held out) from docs/superopt.md.

    scripts/make_string_workloads.py OUTDIR
    scripts/make_string_workloads.py OUTDIR --large 16384

--large N (log 198): held-out `*_eval_large.txt` files of N arrays, so an
external measurement does not repeat a sequence the predictor can learn.

words: identifiers, `w 0` (strlen and the hashes);
numbers: optional blanks and sign, 1-9 digits, sometimes a trailing letter (atoi);
pairs: two strings as halves of one array, each padded with NULs to the same
length: 40% equal, 40% a sorted neighbour (shared prefix), 20% random
(strcmp, memcmp); casepairs: the same with case flipped at random in
30% of the right-hand strings (strcasecmp).
"""
import os
import random
import re
import sys

N = 1024


def words(path):
    return [w for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]{1,30}", open(path, errors="replace").read())]


def pair_lines(ws, rng, flip_case, n=N):
    srt = sorted(set(ws))
    lines = []
    for _ in range(n):
        r = rng.random()
        a = rng.choice(ws)
        if r < 0.4:
            b = a
        elif r < 0.8:
            k = srt.index(a)
            b = srt[min(len(srt) - 1, k + 1)] if rng.random() < 0.5 else srt[max(0, k - 1)]
        else:
            b = rng.choice(ws)
        if flip_case and rng.random() < 0.3:
            b = "".join(c.swapcase() if rng.random() < 0.5 else c for c in b)
        L = max(len(a), len(b)) + 1
        arr = [ord(c) for c in a] + [0] * (L - len(a)) + [ord(c) for c in b] + [0] * (L - len(b))
        lines.append(arr)
    return lines


def number_lines(rng, n=N):
    lines = []
    for _ in range(n):
        s = "".join(rng.choice(" \t") for _ in range(rng.choice([0, 0, 0, 0, 1, 2, 3])))
        r = rng.random()
        s += "-" if r < 0.3 else "+" if r < 0.4 else ""
        s += str(rng.randint(0, 10 ** rng.randint(1, 9) - 1))
        if rng.random() < 0.1:
            s += rng.choice("abcxyz ,.")
        lines.append([ord(c) for c in s] + [0])
    return lines


def write(path, lines):
    with open(path, "w") as f:
        f.write("\n".join(" ".join(map(str, l)) for l in lines) + "\n")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    if len(sys.argv) == 4 and sys.argv[2] == "--large":
        n = int(sys.argv[3])
        ws = words("docs/superopt.md")
        rng = random.Random(3)
        write(os.path.join(out, "words_eval_large.txt"), [[ord(c) for c in rng.choice(ws)] + [0] for _ in range(n)])
        write(os.path.join(out, "numbers_eval_large.txt"), number_lines(rng, n))
        write(os.path.join(out, "pairs_eval_large.txt"), pair_lines(ws, rng, False, n))
        write(os.path.join(out, "casepairs_eval_large.txt"), pair_lines(ws, rng, True, n))
        return
    for part, src, seed in (("search", "README.md", 1), ("eval", "docs/superopt.md", 2)):
        ws = words(src)
        rng = random.Random(seed)
        write(os.path.join(out, f"words_{part}.txt"), [[ord(c) for c in rng.choice(ws)] + [0] for _ in range(N)])
        write(os.path.join(out, f"numbers_{part}.txt"), number_lines(rng))
        write(os.path.join(out, f"pairs_{part}.txt"), pair_lines(ws, rng, False))
        write(os.path.join(out, f"casepairs_{part}.txt"), pair_lines(ws, rng, True))


if __name__ == "__main__":
    main()
