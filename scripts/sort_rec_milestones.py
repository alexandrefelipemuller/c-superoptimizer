"""Section 160: staged milestones toward recursive sorts (merge, quick) over a "programa:" dump (census_dump with recursion
and aux on). Counts per file: how many programs reach each stage. Stages are independent checks on the printed text."""
import re, sys

CALL = re.compile(r"^call\((.*)\);$")
SIMPLE = re.compile(r"^\(?v\d(?: [+-] \d)?\)?$|^\d$")

def split_args(s):
    depth = 0; cur = ""; out = []
    for ch in s:
        if ch in "([": depth += 1
        if ch in ")]": depth -= 1
        if ch == "," and depth == 0: out.append(cur.strip()); cur = ""
        else: cur += ch
    out.append(cur.strip()); return out

def norm(e):
    e = e.strip()
    while e.startswith("(") and e.endswith(")") and e.count("(") == e.count(")") and not re.search(r"\)\s*[-+*/<>=]", e[1:-1] if False else ""):
        inner = e[1:-1]
        d = 0; ok = True
        for ch in inner:
            if ch == "(": d += 1
            if ch == ")":
                d -= 1
                if d < 0: ok = False; break
        if ok and d == 0: e = inner.strip()
        else: break
    return e

def analyse(text):
    lines = [l for l in text.split("\n") if l.strip()]
    stack = []  # (indent, line, calls_in_block list)
    blocks = {}  # id of opening line index -> list of (args)
    r = dict(call=0, clean=0, guarded=0, siblings=0, split=0, mid_div=0, aux_write=0, copyback=0, part_loop=0)
    block_calls = {}
    in_guard = set()
    for idx, l in enumerate(lines):
        ind = len(l) - len(l.lstrip()); s = l.strip()
        while stack and stack[-1][0] >= ind: stack.pop()
        m = CALL.match(s)
        if m:
            r["call"] = 1
            a = split_args(m.group(1))
            if len(a) == 2 and all(SIMPLE.match(norm(x)) or SIMPLE.match(x) for x in a):
                r["clean"] = 1
                key = stack[-1][3] if stack else -1
                block_calls.setdefault(key, []).append(a)
                if any(x[1].startswith("if") for x in stack): r["guarded"] = 1
        if s.endswith("{"): stack.append((ind, s, None, idx))
    for key, cs in block_calls.items():
        if len(cs) >= 2:
            r["siblings"] = 1
            for i in range(len(cs)):
                for j in range(len(cs)):
                    if i != j:
                        mid = {norm(cs[i][1]), norm(cs[i][1]).split(" ")[0]}
                        if norm(cs[j][0]).split(" ")[0] in mid or norm(cs[j][0]) in mid: r["split"] = 1
    if r["split"]:
        if re.search(r"v\d = .*/ 2\)|v\d = .*>> 1\)", text): r["mid_div"] = 1
        if re.search(r"aux\[.*\] = ", text): r["aux_write"] = 1
        if re.search(r"arr\[.*\] = aux\[", text): r["copyback"] = 1
        if re.search(r"swap\(", text) and re.search(r"while", text): r["part_loop"] = 1
    return r

def main():
    names = ["call", "clean", "guarded", "siblings", "split", "mid_div", "aux_write", "copyback", "part_loop"]
    for f in sys.argv[1:]:
        progs = [p for p in open(f).read().split("programa:\n") if p.strip()]
        tot = {k: 0 for k in names}
        extra = dict(merge_like=0, quick_like=0)
        for p in progs:
            a = analyse(p)
            for k in names: tot[k] += a[k]
            if a["split"] and a["mid_div"] and a["aux_write"] and a["copyback"]: extra["merge_like"] += 1
            if a["split"] and a["part_loop"] and a["guarded"]: extra["quick_like"] += 1
        print(f.split("/")[-1], len(progs), *[f"{k}={tot[k]}" for k in names], *[f"{k}={v}" for k, v in extra.items()])
if __name__ == "__main__": main()
