#!/usr/bin/env python3
"""Log 209: run the real preprocessor on a C file, then cut, for each function
of that file, a self-contained unit the superopt front end can import.

`gcc -E` with the project's flags resolves `#if`, macros and `#include`; the
result is one large translation unit full of system declarations the front
end cannot (and need not) parse. For a function F, the unit holds only what F
reaches, in translation-unit order:
  - the typedefs it names (integer ones are expanded by the front end),
  - the struct definitions it names,
  - the functions it calls that are defined in the unit (helpers),
  - its enum constants, as `#define NAME value`,
  - F itself.

    scripts/c_preprocess.py FILE.c [--cflags "-Iinclude -DX"] [--func NAME] [--out DIR]

Without --out, prints the unit of --func (or the list of functions). As a
module: `units(path, cflags)` -> (list of (name, unit_text), error or None).
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

KEYWORDS = {
    "if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default", "goto",
    "sizeof", "static", "inline", "const", "volatile", "extern", "struct", "union", "enum", "typedef", "void",
    "int", "char", "short", "long", "signed", "unsigned", "float", "double", "_Bool",
}


def blank_literals(s):
    """String and char literals blanked (same length), so braces inside them do not count."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < n and s[j] != c and s[j] != "\n":
                j += 2 if s[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * max(j - i - 2, 0) + (c if j - i >= 2 else ""))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def preprocess(path, cflags):
    r = subprocess.run(["gcc", "-E", "-std=gnu11", *cflags, path], capture_output=True, text=True,
                       errors="replace")
    if r.returncode != 0:
        first = next((l for l in r.stderr.splitlines() if "error" in l), r.stderr.strip()[:200])
        return None, re.sub(r"^.*?error: ", "", first)[:90]
    return r.stdout, None


def items(tu, main_file):
    """Top-level items: (kind, names, text, from_main). kind: func, typedef, struct, enum, other."""
    # Line markers name the file each line comes from.
    origin, lines, cur = [], [], ""
    for line in tu.split("\n"):
        m = re.match(r'#\s*\d+\s+"([^"]*)"', line)
        if m:
            cur = m.group(1)
            lines.append("")
        elif line.startswith("#"):
            lines.append("")  # pragmas
        else:
            lines.append(line)
        origin.append(cur)
    text = "\n".join(lines)
    blank = blank_literals(text)
    line_of = []
    for k, line in enumerate(lines):
        line_of.extend([k] * (len(line) + 1))
    main_real = os.path.realpath(main_file)

    def from_main(pos):
        o = origin[line_of[min(pos, len(line_of) - 1)]]
        return bool(o) and os.path.realpath(o) == main_real

    res, depth, paren, start = [], 0, 0, 0
    func_head = None
    i = 0
    while i < len(blank):
        c = blank[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "{":
            if depth == 0:
                head = blank[start:i]
                m = re.search(r"([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*$", head)
                is_aggr = re.search(r"\b(struct|union|enum)\b[^()]*$", head) or "=" in head
                func_head = m.group(1) if (m and not is_aggr and not re.search(r"\btypedef\b", head)) else None
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and func_head:
                body = text[start:i + 1].strip()
                res.append(("func", [func_head], body, from_main(i)))
                start, func_head = i + 1, None
        elif c == ";" and depth == 0 and paren == 0:
            item = text[start:i + 1].strip()
            if item:
                res.append(classify(item) + (item, from_main(i)))
            start = i + 1
        i += 1
    return res


def classify(item):
    flat = " ".join(blank_literals(item).split())
    if re.match(r"typedef\b", flat):
        # typedef ... NAME; or typedef struct {...} NAME, *PNAME;
        tail = flat.rsplit("}", 1)[-1] if "}" in flat else flat
        tail = re.sub(r"\([^()]*\)\s*$", "", tail)  # function-type typedefs: keep the name only
        names = re.findall(r"\(?\s*\**\s*([A-Za-z_]\w*)\s*\)?\s*(?:\[[^\]]*\])?\s*[,;]", tail)
        tags = re.findall(r"\b(?:struct|union)\s+([A-Za-z_]\w*)\s*\{", flat)
        enums = enum_constants(flat)
        return ("typedef", names + ["struct " + t for t in tags] + [n for n, _ in enums])
    m = re.match(r"(?:struct|union)\s+([A-Za-z_]\w*)\s*\{", flat)
    if m:
        return ("struct", ["struct " + m.group(1)])
    if re.match(r"enum\b[^;]*\{", flat):
        return ("enum", [n for n, _ in enum_constants(flat)])
    return ("other", [])


def enum_constants(flat):
    m = re.search(r"\benum\b[^{]*\{(.*)\}", flat)
    if not m:
        return []
    out, value, known = [], -1, {}
    for part in split_top(m.group(1)):
        part = part.strip()
        if not part:
            continue
        name, _, expr = part.partition("=")
        name = name.strip()
        if expr.strip():
            value = eval_const(expr, known)
        else:
            value = None if value is None else value + 1
        known[name] = value
        out.append((name, value))
    return out


def split_top(s):
    parts, depth, cur = [], 0, ""
    for c in s:
        depth += c in "([" and 1 or (c in ")]" and -1 or 0)
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    return parts + [cur]


def eval_const(expr, known):
    e = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b", r"\1", expr.strip())
    e = re.sub(r"\(\s*(?:unsigned|signed|int|long|short|char|\s)+\)", "", e)
    for name, v in known.items():
        if v is not None:
            e = re.sub(r"\b%s\b" % re.escape(name), "(%d)" % v, e)
    if not re.fullmatch(r"[0-9xXa-fA-F\s()+\-*/%<>&|^~]*", e):
        return None
    try:
        v = eval(e.replace("/", "//"), {"__builtins__": {}})
    except Exception:
        return None
    return v if -2**31 <= v < 2**32 else None


def idents(text):
    b = blank_literals(text)
    return set(re.findall(r"\bstruct\s+[A-Za-z_]\w*|\bunion\s+[A-Za-z_]\w*|[A-Za-z_]\w*", b))


def unit_for(target, all_items):
    """The self-contained text for function `target` (an index into all_items)."""
    by_name = {}
    for k, (kind, names, _, _) in enumerate(all_items):
        for n in names:
            # The definition wins over a declaration or a forward typedef.
            if n not in by_name or kind in ("func", "struct") or all_items[by_name[n]][0] == "other":
                by_name[n] = k
    enum_value = {}
    for kind, names, text, _ in all_items:
        if kind in ("enum", "typedef"):
            for n, v in enum_constants(" ".join(blank_literals(text).split())):
                enum_value[n] = v
    need, defines, todo = {target}, {}, [target]
    while todo:
        k = todo.pop()
        for w in idents(all_items[k][2]):
            if w in KEYWORDS:
                continue
            if w in enum_value and all_items[by_name.get(w, target)][0] in ("enum", "typedef"):
                if enum_value[w] is not None:
                    defines[w] = enum_value[w]
                continue
            j = by_name.get(w)
            if j is not None and j not in need and all_items[j][0] in ("func", "typedef", "struct"):
                need.add(j)
                todo.append(j)
    parts = ["#define %s %d" % (n, v) for n, v in sorted(defines.items())]
    # Translation-unit order, so the unit compiles as the original did (log
    # 210: reordering broke 8 Mbed TLS units); the front end binds a
    # `typedef struct S T;` that precedes `struct S {...}`.
    for k in sorted(need):
        kind, names, text, _ = all_items[k]
        if kind == "typedef" and any(n in enum_value for n in names):
            continue  # a typedef'd enum: its constants are the #defines above, its type is int
        parts.append(text)
    return "\n".join(parts) + "\n"


def units(path, cflags):
    tu, err = preprocess(path, cflags)
    if tu is None:
        return [], "preprocess failed: " + err
    all_items = items(tu, path)
    out = []
    for k, (kind, names, _, main) in enumerate(all_items):
        if kind == "func" and main:
            out.append((names[0], unit_for(k, all_items)))
    return out, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--cflags", default="")
    ap.add_argument("--func", default=None)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    res, err = units(a.file, shlex.split(a.cflags))
    if err:
        print(err, file=sys.stderr)
        sys.exit(1)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for name, text in res:
            if a.func in (None, name):
                open(os.path.join(a.out, name + ".c"), "w").write(text)
    elif a.func:
        for name, text in res:
            if name == a.func:
                sys.stdout.write(text)
                return
        print("no function " + a.func, file=sys.stderr)
        sys.exit(1)
    else:
        for name, _ in res:
            print(name)


if __name__ == "__main__":
    main()
