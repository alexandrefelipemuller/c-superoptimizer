#!/usr/bin/env python3
"""Section 192: how much real C the superopt front end accepts.

Splits C files into their top-level function definitions, writes each one
alone to a file and runs `algosearch c-import` on it. Prints one TSV line per
function (file, name, OK or the reason) and, on stderr, the reasons ranked.

    scripts/c_census.py --out census.tsv --keep DIR SRC_DIR_OR_FILE...

With --keep, every function that imports is saved as DIR/<file>__<name>.c.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile


def strip(src):
    """Comments, string and char literals blanked (same length, newlines kept)."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else src[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(src):
    """(name, text) for every top-level function definition."""
    s = strip(src)
    # Preprocessor lines (with continuations) do not count as code.
    s = re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), s)
    res, depth, start, i = [], 0, 0, 0
    while i < len(s):
        c = s[i]
        if c == "{":
            if depth == 0:
                head = s[start:i]
                m = re.search(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", head)
                body_start = i
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if m and not re.search(r"\b(struct|union|enum|typedef)\b|=", head):
                    first = start + len(head) - len(head.lstrip())
                    res.append((m.group(1), src[first:i + 1]))
                start = i + 1
                m = None
        elif c == ";" and depth == 0:
            start = i + 1
        i += 1
    return res


def reason(msg):
    msg = re.sub(r"^.*?: line \d+: ", "", msg.strip().splitlines()[-1] if msg.strip() else "no output")
    msg = re.sub(r"'[^']*' not supported in this prototype", lambda m: m.group(0), msg)
    msg = re.sub(r"call to '[^']*'", "call to a function not in the file", msg)
    msg = re.sub(r"unknown identifier '[^']*'", "unknown identifier (macro/global)", msg)
    msg = re.sub(r"(found|expected an identifier, found|unexpected) '[^']*'", r"\1 X", msg)
    msg = re.sub(r"type '[^']*'", "type X", msg)
    msg = re.sub(r"'[^']*' is not the array parameter", "indexing a non-parameter", msg)
    msg = re.sub(r"unexpected character '.'", "unexpected character", msg)
    msg = re.sub(r"integer literal suffix not supported: \S+", "integer literal suffix", msg)
    return msg[:90]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--algosearch", default="./algosearch")
    ap.add_argument("--out", required=True)
    ap.add_argument("--keep", default=None)
    args = ap.parse_args()
    files = []
    for p in args.paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, f) for f in sorted(names) if f.endswith(".c")]
        else:
            files.append(p)
    if args.keep:
        os.makedirs(args.keep, exist_ok=True)
    reasons = collections.Counter()
    total = ok = 0
    with open(args.out, "w") as out, tempfile.TemporaryDirectory() as tmp:
        out.write("file\tfunction\tstatus\n")
        for f in sorted(files):
            try:
                src = open(f, errors="replace").read()
            except OSError:
                continue
            for name, text in functions(src):
                total += 1
                path = os.path.join(tmp, "f.c")
                with open(path, "w") as g:
                    g.write(text + "\n")
                r = subprocess.run([args.algosearch, "c-import", path], capture_output=True, text=True)
                if r.returncode == 0:
                    ok += 1
                    status = "OK"
                    if args.keep:
                        tag = os.path.basename(f)[:-2] + "__" + name
                        with open(os.path.join(args.keep, tag + ".c"), "w") as g:
                            g.write(f"/* {f} : {name} */\n" + text + "\n")
                else:
                    status = reason(r.stderr or r.stdout)
                    reasons[status] += 1
                out.write(f"{f}\t{name}\t{status}\n")
    print(f"{ok}/{total} functions import", file=sys.stderr)
    for msg, k in reasons.most_common(25):
        print(f"{k:6d}  {msg}", file=sys.stderr)


if __name__ == "__main__":
    main()
