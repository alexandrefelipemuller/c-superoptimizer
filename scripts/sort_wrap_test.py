"""Section 157: directed step. Take fresh candidates that already contain the adjacent compare-swap pass (milestone
'adjacent') and wrap the whole program in `vK = 0; while (vK < v0) { <program>; vK = vK + 1; }` (vK unused). Count how many
then sort every array of length 2..5 over {0,1,2}. usage: sort_wrap_test.py FILE... """
import itertools, re, sys
sys.argv_save = sys.argv
src = open(__file__.replace("sort_wrap_test", "sort_milestones")).read().replace("\nmain()\n", "\n")
exec(src)

def to_py(text, extra=None):
    out = []; ind = 0
    for l in text.strip().split("\n"):
        s = l.strip()
        if s == "}": ind -= 1; continue
        if s.startswith("} else"): ind -= 1; out.append("    " * ind + "else:"); ind += 1; continue
        m = re.match(r"(while|if) \((.*)\) \{$", s)
        if m: out.append("    " * ind + f"{m.group(1)} {m.group(2)}:"); ind += 1; continue
        m = re.match(r"swap\(arr\[(.*)\], arr\[(.*)\]\);$", s)
        if m: out.append("    " * ind + f"sw({m.group(1)},{m.group(2)})"); continue
        out.append("    " * ind + s.rstrip(";"))
    return "\n".join(out)

def run(code, nvars, arr):
    steps = [0]
    def sw(i, j):
        if not (0 <= i < len(arr) and 0 <= j < len(arr)): raise IndexError
        arr[i], arr[j] = arr[j], arr[i]
    env = {"sw": sw}
    class A(list):
        def __getitem__(s, i):
            if i < 0 or i >= len(s): raise IndexError
            return list.__getitem__(s, i)
        def __setitem__(s, i, v):
            if i < 0 or i >= len(s): raise IndexError
            list.__setitem__(s, i, v)
    a = A(arr)
    def sw2(i, j):
        if not (0 <= i < len(a) and 0 <= j < len(a)): raise IndexError
        a[i], a[j] = a[j], a[i]
    env = {"sw": sw2, "arr": a}
    for k in range(nvars): env[f"v{k}"] = 0
    env["v0"] = len(arr)
    import sys as _s
    cnt = [0]
    def tracer(frame, event, arg):
        cnt[0] += 1
        if cnt[0] > 20000: raise TimeoutError
        return tracer
    _s.settrace(tracer)
    try:
        exec(code, env)
    finally:
        _s.settrace(None)
    return list(a)

def sorts(code, nv):
    for n in range(2, 6):
        for t in itertools.product(range(3), repeat=n):
            try:
                if run(code, nv, list(t)) != sorted(t): return False
            except Exception:
                return False
    return True

NV = 8
for f in sys.argv[1:]:
    progs = [p for p in open(f).read().split("programa:\n") if p.strip()]
    adj = [p for p in progs if analyse(p)["adjacent"] and "/" not in p and "%" not in p]
    base = wrap = 0
    for p in adj:
        used = {int(x) for x in re.findall(r"v(\d)", p)}
        free = [k for k in range(1, NV) if k not in used]
        if not free: continue
        k = free[0]
        if sorts(to_py(p), NV): base += 1
        body = "\n".join("    " + l for l in to_py(p).split("\n"))
        code = f"v{k} = 0\nwhile v{k} < v0:\n{body}\n    v{k} = v{k} + 1"
        if sorts(code, NV): wrap += 1
    print(f.split("/")[-1], "programas", len(progs), "com passada adjacente", len(adj), "ordenam sozinhos", base, "ordenam envoltos num laco externo", wrap)
