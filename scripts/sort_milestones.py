"""Section 157: staged milestones toward comparison sorts, counted over a "programa:" dump (sort_census/census_dump).
usage: sort_milestones.py FILE...   (one column per file). Milestones are cumulative structural checks on printed text."""
import re, sys

CMP = re.compile(r"^(?:if|while) \(\((arr\[.*?\]) (<|>|<=|>=) (arr\[.*?\])\)\) \{$")
SW = re.compile(r"^swap\((arr\[.*?\]), (arr\[.*?\])\);$")

def analyse(text):
    lines = [l for l in text.split("\n") if l.strip()]
    stack = []  # (indent, line)
    r = dict(loop_swap=0, cmp_swap=0, same_pair=0, nested=0, adjacent=0, counters=0)
    for l in lines:
        ind = len(l) - len(l.lstrip()); s = l.strip()
        while stack and stack[-1][0] >= ind: stack.pop()
        m = SW.match(s)
        if m:
            loops = [x for x in stack if x[1].startswith("while")]
            ifs = [x for x in stack if x[1].startswith("if")]
            if loops: r["loop_swap"] = 1
            for _, c in ifs:
                cm = CMP.match(c)
                if cm:
                    r["cmp_swap"] = 1
                    pair = {cm.group(1), cm.group(3)}
                    if pair == {m.group(1), m.group(2)}:
                        r["same_pair"] = 1
                        if len(loops) >= 2: r["nested"] = 1
                        idx = [re.sub(r"^arr\[|\]$", "", g) for g in (m.group(1), m.group(2))]
                        a, b = idx
                        if re.fullmatch(re.escape(a) + r" [+-] 1|\(" + re.escape(a) + r" [+-] 1\)", b) or \
                           re.fullmatch(re.escape(b) + r" [+-] 1|\(" + re.escape(b) + r" [+-] 1\)", a):
                            r["adjacent"] = 1
        if s.endswith("{"): stack.append((ind, s))
    # counters: every while has a body line v = (v +/- 1)
    if r["nested"] and len(re.findall(r"v\d = \(v\d [+-] 1\);", text)) >= 2: r["counters"] = 1
    return r

def main():
    names = ["loop_swap", "cmp_swap", "same_pair", "nested", "adjacent", "counters"]
    for f in sys.argv[1:]:
        progs = [p for p in open(f).read().split("programa:\n") if p.strip()]
        tot = {k: 0 for k in names}
        for p in progs:
            a = analyse(p)
            for k in names: tot[k] += a[k]
        print(f, len(progs), *[f"{k}={tot[k]}" for k in names])
main()
