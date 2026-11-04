#!/usr/bin/env python3
"""Section 156: frequency of the loop skeletons among fresh candidates (programs from skeleton_census_dump).

driver = a variable in a while condition that the loop body reassigns from itself (i = i + 1, x = x / 10, ...).
S1 (array-update loop):  driver init before the loop, body has a store arr[idx] with idx using the driver, driver step.
S2 (scalar-recurrence loop): driver init before the loop, body has a SECOND self-referential assign (accumulator a),
     and after the loop a store arr[..] = expression using a.
Both are counted with the holes unconstrained (any expression in the init, step, update and store index/value).
"""
import collections
import sys

sys.path.insert(0, "scripts")
from mine_fragments import parse_program  # noqa: E402


def vars_of(n, out):
    if isinstance(n, tuple):
        if n and n[0] == "var":
            out.add(n[1])
        else:
            for c in n:
                vars_of(c, out)
    return out


def clean(prog):
    """Clean shapes: a top-level while whose body is EXACTLY the skeleton (holes anywhere in the expressions).
    C1: body = {arr[idx(d)] = e ; d = d (+|-) const}   in either order (prefix_sums, adjacent_differences)
    C2: body = {a = f(a,..) ; d = g(d)}                 in either order, then a store using a (digital_root-like loops,
        factorial_trailing_zeros, reverse_number, lcm's gcd half, josephus)
    The loop must be preceded by an assignment of d (the init is a hole too) -- or d may be left at its initial value 0.
    """
    c1 = c2 = False
    for k, s in enumerate(prog):
        if s[0] != "while" or len(s[2]) != 2 or s[3]:
            continue
        a, b = s[2]
        cond_vars = vars_of(s[1], set())
        for x, y in ((a, b), (b, a)):
            if y[0] == "assign" and y[1] in cond_vars and y[1] in vars_of(y[2], set()):
                d = y[1]
                if x[0] == "astore" and d in vars_of(x[1], set()):
                    c1 = True
                if x[0] == "assign" and x[1] != d and x[1] in vars_of(x[2], set()):
                    after = prog[k + 1:]
                    if any(t[0] == "astore" and x[1] in vars_of(t[2], set()) for t in after):
                        c2 = True
    return c1, c2


def classify(prog):
    s1 = s2 = False
    s1_any = s2_any = False

    def walk(stmts):
        nonlocal s1, s2, s1_any, s2_any
        for k, s in enumerate(stmts):
            if s[0] == "while":
                cond_vars = vars_of(s[1], set())
                body = s[2]
                selfref = {}
                for b in body:
                    if b[0] == "assign" and b[1] in vars_of(b[2], set()):
                        selfref[b[1]] = b
                drivers = [v for v in cond_vars if v in selfref]
                inits = {t[1] for t in stmts[:k] if t[0] == "assign"}
                for d in drivers:
                    stores = [b for b in body if b[0] == "astore" and d in vars_of(b[1], set())]
                    if stores:
                        s1_any = True
                        if d in inits:
                            s1 = True
                    accs = [a for a in selfref if a != d]
                    if accs:
                        s2_any = True
                        after = stmts[k + 1:]
                        used = any(t[0] == "astore" and a in vars_of(t[2], set()) for t in after for a in accs)
                        if d in inits and used:
                            s2 = True
                walk(s[2])
                walk(s[3])
            elif s[0] == "if":
                walk(s[2])
                walk(s[3])

    walk(prog)
    return s1, s2, s1_any, s2_any


def main():
    stats = collections.Counter()
    n = 0
    for path in sys.argv[1:]:
        text = open(path).read().split("programa:\n")[1:]
        size = int(path.split("size")[-1].split(".")[0])
        c = collections.Counter()
        total = 0
        for t in text:
            try:
                prog = parse_program(t.strip("\n") + "\n")
            except Exception:
                c["parse_fail"] += 1
                continue
            total += 1
            r = classify(prog) + clean(prog)
            for name, v in zip(("S1", "S2", "S1loose", "S2loose", "C1", "C2"), r):
                if v:
                    c[name] += 1
        print("size %2d  n=%5d  S1 %.4f  S2 %.4f   (loose: S1 %.4f  S2 %.4f)  clean: C1 %.5f  C2 %.5f  parse_fail=%d" % (
            size, total, c["S1"] / total, c["S2"] / total, c["S1loose"] / total, c["S2loose"] / total,
            c["C1"] / total, c["C2"] / total, c["parse_fail"]))


if __name__ == "__main__":
    main()
