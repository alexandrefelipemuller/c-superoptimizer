#!/usr/bin/env python3
"""Section 154: coverage of known solutions by fragments mined from other problems' solved programs.

Corpus: solved programs printed by the search ("programa:" blocks) in the 77-problem battery logs
(data/digit_pieces_joint_battery_20261101/log_base_<problem>_<seed>.txt) and in the learning-curve batches
(data/learning_curve_b*/beam_on/fresh_<problem>/seed_*.log). Programs are parsed to trees, variables are renamed by
first occurrence, and every statement is a fragment. A fragment's support is the number of DISTINCT problems whose
solved programs contain it. For a target problem, the library is built WITHOUT that problem (leave-one-problem-out).
Two levels of match: exact (variables renamed, constants kept) and loose (constants other than 0 and 1 abstracted).
"""
import collections
import glob
import os
import re
import sys

TOK = re.compile(r"\s*(arr\[|aux\[|<<|>>|<=|>=|==|!=|[-+*/%&|^<>()\]]|v\d+|-?\d+)")


def parse_expr(s, i=0):
    """Fully parenthesised expressions as printed by PrettyPrinter."""
    m = TOK.match(s, i)
    if not m:
        raise ValueError("bad expr at %d: %r" % (i, s[i:i + 30]))
    t = m.group(1)
    i = m.end()
    if t == "(":
        a, i = parse_expr(s, i)
        m = TOK.match(s, i)
        op = m.group(1)
        i = m.end()
        b, i = parse_expr(s, i)
        m = TOK.match(s, i)
        assert m.group(1) == ")", s[i:i + 20]
        return ("op", op, a, b), m.end()
    if t in ("arr[", "aux["):
        a, i = parse_expr(s, i)
        m = TOK.match(s, i)
        assert m.group(1) == "]"
        return (t[:-1], a), m.end()
    if t.startswith("v"):
        return ("var", int(t[1:])), i
    return ("const", int(t)), i


def parse_program(text):
    lines = [l.rstrip() for l in text.split("\n") if l.strip()]
    pos = [0]

    def block():
        out = []
        while pos[0] < len(lines):
            l = lines[pos[0]].strip()
            if l.startswith("}"):
                return out
            pos[0] += 1
            if l.startswith("if (") or l.startswith("while ("):
                kind = "if" if l.startswith("if") else "while"
                cond, _ = parse_expr(l[l.index("(") + 1:l.rindex(")")], 0)
                body = block()
                els = []
                closing = lines[pos[0]].strip()
                pos[0] += 1
                if closing.startswith("} else"):
                    els = block()
                    pos[0] += 1
                out.append((kind, cond, tuple(body), tuple(els)))
            elif l.startswith("swap("):
                inner = l[len("swap("):-2]
                a, j = parse_expr(inner[len("arr["):], 0)
                rest = inner[len("arr["):][j:]
                rest = rest[rest.index("arr[") + 4:]
                b, _ = parse_expr(rest, 0)
                out.append(("swap", a, b))
            elif l.startswith("arr["):
                idx, j = parse_expr(l[4:], 0)
                rest = l[4 + j:]
                val, _ = parse_expr(rest[rest.index("=") + 1:].rstrip(";"), 0)
                out.append(("astore", idx, val))
            elif l.startswith("aux["):
                idx, j = parse_expr(l[4:], 0)
                rest = l[4 + j:]
                val, _ = parse_expr(rest[rest.index("=") + 1:].rstrip(";"), 0)
                out.append(("xstore", idx, val))
            elif re.match(r"v\d+ = ", l):
                var = int(l[1:l.index(" ")])
                val, _ = parse_expr(l[l.index("=") + 1:].rstrip(";"), 0)
                out.append(("assign", var, val))
            else:
                out.append(("other", l))
        return out

    return tuple(block())


def simp(e):
    """Drop `0 + x` and `x + 0` index padding; keep the rest."""
    if e[0] == "op":
        a, b = simp(e[2]), simp(e[3])
        if e[1] == "+" and a == ("const", 0):
            return b
        if e[1] == "+" and b == ("const", 0):
            return a
        return ("op", e[1], a, b)
    if e[0] in ("arr", "aux"):
        return (e[0], simp(e[1]))
    return e


def rename(node, mapping, loose):
    """Rename variables by first occurrence (v0 stays v0); optionally abstract constants other than 0 and 1."""
    if isinstance(node, tuple) and node and node[0] == "var":
        v = node[1]
        if v == 0:
            return ("var", 0)
        if v not in mapping:
            mapping[v] = len(mapping) + 1
        return ("var", mapping[v])
    if isinstance(node, tuple) and node and node[0] == "const":
        return ("const", node[1]) if (not loose or node[1] in (0, 1)) else ("const", "K")
    if isinstance(node, tuple):
        if node and node[0] == "assign":
            v = node[1]
            if v != 0 and v not in mapping:
                mapping[v] = len(mapping) + 1
            nv = 0 if v == 0 else mapping[v]
            return ("assign", nv, rename(simp(node[2]), mapping, loose))
        if node and node[0] in ("if", "while"):
            return (node[0], rename(simp(node[1]), mapping, loose),
                    tuple(rename(c, mapping, loose) for c in node[2]),
                    tuple(rename(c, mapping, loose) for c in node[3]))
        if node and node[0] in ("astore", "xstore"):
            return (node[0], rename(simp(node[1]), mapping, loose), rename(simp(node[2]), mapping, loose))
        if node and node[0] == "swap":
            return ("swap", rename(simp(node[1]), mapping, loose), rename(simp(node[2]), mapping, loose))
        return tuple(rename(c, mapping, loose) if isinstance(c, tuple) else c for c in node)
    return node


def fragments(prog, loose):
    """Every statement (with its nested body) of the program, each renamed on its own."""
    out = []

    def walk(stmts):
        for s in stmts:
            out.append(rename(s, {}, loose))
            if s[0] in ("if", "while"):
                walk(s[2])
                walk(s[3])

    walk(prog)
    return out


def size(f):
    if isinstance(f, tuple):
        return 1 + sum(size(c) for c in f if isinstance(c, tuple))
    return 1


def read_programs(path):
    text = open(path, errors="replace").read()
    progs = []
    for m in re.finditer(r"programa:\n(.*?)\n\n", text, re.S):
        progs.append(m.group(1))
    return progs


def corpus():
    c = collections.defaultdict(list)
    for f in sorted(glob.glob("data/digit_pieces_joint_battery_20261101/log_base_*_*.txt")):
        name = re.match(r".*log_base_(.*)_\d+\.txt", f).group(1)
        for p in read_programs(f):
            c[name].append(p)
    for b in ("b1", "b2", "b3"):
        for f in sorted(glob.glob("data/learning_curve_%s_*/beam_on/fresh_*/seed_*.log" % b)):
            name = f.split("/")[-2]
            for p in read_programs(f):
                c[name].append(p)
    return c


# Known solutions of problems the search did not solve, written in the printer's format (sections 149c, 150s, 151).
KNOWN = {
    "fresh_digital_root": "arr[0] = (((arr[0] - 1) % 9) + 1);\n",
    "fresh_prefix_sums": "v1 = 1;\nwhile ((v1 < v0)) {\n  arr[v1] = (arr[v1] + arr[(v1 - 1)]);\n  v1 = (v1 + 1);\n}\n",
    "fresh_lcm": "v1 = arr[0];\nv2 = arr[1];\nwhile ((v2 != 0)) {\n  v3 = (v1 % v2);\n  v1 = v2;\n  v2 = v3;\n}\narr[0] = ((arr[0] / v1) * arr[1]);\n",
    "fresh_josephus": "v1 = arr[0];\nv2 = arr[1];\nv3 = 0;\nv4 = 2;\nwhile ((v4 <= v1)) {\n  v3 = ((v3 + v2) % v4);\n  v4 = (v4 + 1);\n}\narr[0] = (v3 + 1);\n",
    "fresh_adjacent_differences": "v1 = (v0 - 1);\nwhile ((v1 > 0)) {\n  arr[v1] = (arr[v1] - arr[(v1 - 1)]);\n  v1 = (v1 - 1);\n}\n",
    "fresh_factorial_trailing_zeros": "v1 = arr[0];\nv2 = 0;\nwhile ((v1 > 0)) {\n  v1 = (v1 / 5);\n  v2 = (v2 + v1);\n}\narr[0] = v2;\n",
    "fresh_reverse_number": "v1 = arr[0];\nv2 = 0;\nwhile ((v1 > 0)) {\n  v2 = ((v2 * 10) + (v1 % 10));\n  v1 = (v1 / 10);\n}\narr[0] = v2;\n",
    "fresh_dot_product_halves": "v1 = (v0 / 2);\nv2 = 0;\nv3 = 0;\nwhile ((v2 < v1)) {\n  v3 = (v3 + (arr[v2] * arr[(v1 + v2)]));\n  v2 = (v2 + 1);\n}\narr[0] = v3;\n",
    "fresh_matrix_transpose": "v1 = 0;\nwhile ((((v1 + 1) * (v1 + 1)) <= v0)) {\n  v1 = (v1 + 1);\n}\nv2 = 0;\nwhile ((v2 < v1)) {\n  v3 = (v2 + 1);\n  while ((v3 < v1)) {\n    v4 = arr[((v2 * v1) + v3)];\n    arr[((v2 * v1) + v3)] = arr[((v3 * v1) + v2)];\n    arr[((v3 * v1) + v2)] = v4;\n    v3 = (v3 + 1);\n  }\n  v2 = (v2 + 1);\n}\n",
}


def main():
    c = corpus()
    print("corpus: %d problems, %d programs" % (len(c), sum(len(v) for v in c.values())))
    for loose in (False, True):
        support = collections.defaultdict(set)
        for name, progs in c.items():
            seen = set()
            for p in progs:
                try:
                    tree = parse_program(p)
                except Exception:
                    continue
                for fr in fragments(tree, loose):
                    seen.add(fr)
            for fr in seen:
                support[fr].add(name)
        print("\n== %s match: %d distinct fragments" % ("loose" if loose else "exact", len(support)))
        # targets: unsolved known solutions, plus every solved fresh problem (from its own solved programs, shortest)
        targets = dict((k, v) for k, v in KNOWN.items())
        for name, progs in c.items():
            if name.startswith("fresh_") and name not in targets and progs:
                targets[name] = min(progs, key=len)
        for name in sorted(targets):
            try:
                tree = parse_program(targets[name])
            except Exception as e:
                print("%-34s parse error %s" % (name, e))
                continue
            frs = [f for f in fragments(tree, loose) if size(f) >= 4]
            if not frs:
                print("%-34s no fragments of size>=4" % name)
                continue
            # leave-one-problem-out support
            cov = [len(support.get(f, set()) - {name}) for f in frs]
            full = max(range(len(frs)), key=lambda i: size(frs[i]))  # the largest fragment
            print("%-34s %s frags=%2d covered(>=1 other problem)=%2d  >=3 problems=%2d  largest covered by %2d problems"
                  % (name, "UNSOLVED" if name in KNOWN else "solved  ", len(frs), sum(1 for x in cov if x >= 1),
                     sum(1 for x in cov if x >= 3), cov[full]))


if __name__ == "__main__":
    main()
