#!/usr/bin/env python3
"""Log 204: scalar regions inside real C functions.

Whole scalar functions are rare in real code (log 200: 11 of 5,766 import
alone). This finds the pure scalar *expressions* inside every function: the
conditions of `if`/`while`, `return` values, assignment right-hand sides and
initializers, and their maximal pure sub-expressions. A region may read:
  - scalar parameters and locals of a supported integer type;
  - loads `p[e]` / `*p` through a parameter or local declared as a pointer to
    a supported integer type (one input per distinct load text; a pure
    expression has no side effect between two equal loads).
Calls, struct members, macros and globals (unknown identifiers), `long`,
floating point, `sizeof`, assignments and `++`/`--` are opaque: a region is a
maximal subtree with none of them.

Each region with at least --min-ops operators and 1..--max-inputs inputs is
written as a standalone function

    /* FILE:LINE in FUNCTION: original text */
    static int region(T a, ...) { return EXPR; }

and checked with `algosearch c-import`. Typedef names are mapped by the table
TYPES below (an assumption, e.g. sqlite's u8 = unsigned char).

    scripts/c_regions.py --out regions.tsv --keep DIR [--tag NAME] SRC...
"""
import argparse
import collections
import hashlib
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

_spec = importlib.util.spec_from_file_location("c_census", os.path.join(os.path.dirname(__file__), "c_census.py"))
c_census = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c_census)

# C spelling -> (spelling to emit, bits, signed)
TYPES = {
    "int": ("int", 32, True), "signed": ("int", 32, True), "signed int": ("int", 32, True),
    "unsigned": ("unsigned", 32, False), "unsigned int": ("unsigned", 32, False),
    "char": ("char", 8, True), "signed char": ("signed char", 8, True), "unsigned char": ("unsigned char", 8, False),
    "short": ("short", 16, True), "short int": ("short", 16, True), "signed short": ("short", 16, True),
    "unsigned short": ("unsigned short", 16, False), "unsigned short int": ("unsigned short", 16, False),
    "int8_t": ("signed char", 8, True), "uint8_t": ("unsigned char", 8, False),
    "int16_t": ("short", 16, True), "uint16_t": ("unsigned short", 16, False),
    "int32_t": ("int", 32, True), "uint32_t": ("unsigned", 32, False),
    # project typedefs (assumed from their headers)
    "u8": ("unsigned char", 8, False), "u16": ("unsigned short", 16, False), "u32": ("unsigned", 32, False),
    "i8": ("signed char", 8, True), "i16": ("short", 16, True),
    "png_byte": ("unsigned char", 8, False), "png_uint_16": ("unsigned short", 16, False),
    "png_uint_32": ("unsigned", 32, False), "png_int_32": ("int", 32, True),
    "Byte": ("unsigned char", 8, False), "uInt": ("unsigned", 32, False), "uch": ("unsigned char", 8, False),
    "ush": ("unsigned short", 16, False), "BYTE": ("unsigned char", 8, False), "U16": ("unsigned short", 16, False),
    "U32": ("unsigned", 32, False), "S32": ("int", 32, True), "lu_byte": ("unsigned char", 8, False),
}
QUALIFIERS = {"const", "volatile", "register", "static", "inline", "restrict", "__restrict", "auto", "extern"}
TYPE_WORDS = {"int", "signed", "unsigned", "char", "short", "long", "float", "double", "void", "size_t",
              "_Bool", "bool"} | set(TYPES)
INT, UNS = ("int", 32, True), ("unsigned", 32, False)

PUNCT = sorted("""<<= >>= ... -> ++ -- << >> <= >= == != && || += -= *= /= %= &= |= ^= ## + - * / % & | ^ ~ ! < > = ? : ; , . ( ) [ ] { } #""".split(),
               key=len, reverse=True)


def tokenize(src):
    """(kind, text, line, value) tokens; comments dropped, preprocessor lines dropped."""
    toks, i, n, line = [], 0, len(src), 1
    at_line_start = True
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            at_line_start = True
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if at_line_start and c == "#":
            while i < n and src[i] != "\n":
                if src[i] == "\\" and i + 1 < n and src[i + 1] == "\n":
                    line += 1
                    i += 2
                    continue
                i += 1
            continue
        at_line_start = False
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += src.count("\n", i, j)
            i = j
            continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            toks.append(("str", src[i:j + 1], line, None, i))
            i = j + 1
            continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            body = src[i + 1:j]
            val = None
            esc = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39, '"': 34, "a": 7, "b": 8, "f": 12, "v": 11}
            if len(body) == 1:
                val = ord(body)
            elif len(body) == 2 and body[0] == "\\" and body[1] in esc:
                val = esc[body[1]]
            elif re.fullmatch(r"\\x[0-9a-fA-F]{1,2}", body):
                val = int(body[2:], 16)
            elif re.fullmatch(r"\\[0-7]{1,3}", body):
                val = int(body[1:], 8)
            if val is not None and val > 127:
                val -= 256  # plain char is signed here
            toks.append(("char", src[i:j + 1], line, val, i))
            i = j + 1
            continue
        m = re.match(r"(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)", src[i:]) if c.isdigit() else None
        if m and not re.match(r"[\d.]*[.eE]", src[i:i + len(m.group(0)) + 1][len(m.group(0)):] or "x"):
            toks.append(("num", m.group(0), line, m, i))
            i += len(m.group(0))
            continue
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            m2 = re.match(r"[0-9a-fA-FxX.eEpP+\-uUlLfF]+", src[i:])
            toks.append(("float", m2.group(0), line, None, i))
            i += len(m2.group(0))
            continue
        if c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_]\w*", src[i:])
            toks.append(("id", m.group(0), line, None, i))
            i += len(m.group(0))
            continue
        for p in PUNCT:
            if src.startswith(p, i):
                toks.append(("p", p, line, None, i))
                i += len(p)
                break
        else:
            toks.append(("p", c, line, None, i))
            i += 1
    return toks


def parse_type(words):
    """('int',32,True)-like for a list of type words, or None if unsupported."""
    w = [x for x in words if x not in QUALIFIERS]
    key = " ".join(w)
    return TYPES.get(key)


# --------------------------------------------------------------- expressions

class Node:
    __slots__ = ("kind", "op", "kids", "text", "type", "pure", "value", "ops", "span")

    def __init__(self, kind, op=None, kids=(), text="", type_=None, pure=True, value=None):
        self.kind, self.op, self.kids, self.text = kind, op, list(kids), text
        self.span = None  # [first, end) token indices in the parsed span (log 204b)
        self.type, self.pure, self.value = type_, pure, value
        self.ops = (1 if kind in ("bin", "un", "tern", "cast") else 0) + sum(k.ops for k in self.kids)


def promote(t):
    if t is None:
        return None
    return t if t[1] >= 32 else INT


def arith(a, b):
    a, b = promote(a), promote(b)
    if a is None or b is None:
        return None
    return UNS if (not a[2] or not b[2]) else INT


BIN = {"*": 13, "/": 13, "%": 13, "+": 12, "-": 12, "<<": 11, ">>": 11, "<": 10, ">": 10, "<=": 10, ">=": 10,
       "==": 9, "!=": 9, "&": 8, "^": 7, "|": 6, "&&": 5, "||": 4}
ASSIGN = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}


def spanned(f):
    def g(self, *a):
        start = self.i
        n = f(self, *a)
        if n.span is None:
            n.span = (start, self.i)
        return n
    return g


class Parser:
    def __init__(self, toks, scope):
        self.t, self.i, self.scope = toks, 0, scope

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else ("end", "", 0, None)

    def take(self):
        tok = self.peek()
        self.i += 1
        return tok

    def expr(self):  # comma expression: opaque if a comma appears
        e = self.assign()
        while self.peek()[1] == ",":
            self.take()
            r = self.assign()
            e = Node("opaque", kids=[e, r], pure=False)
        return e

    def assign(self):
        e = self.cond()
        if self.peek()[1] in ASSIGN:
            self.take()
            r = self.assign()
            return Node("opaque", kids=[e, r], pure=False)
        return e

    @spanned
    def cond(self):
        c = self.binary(4)
        if self.peek()[1] == "?":
            self.take()
            a = self.expr()
            if self.peek()[1] != ":":
                raise SyntaxError("?:")
            self.take()
            b = self.cond()
            n = Node("tern", "?:", [c, a, b], type_=arith(a.type, b.type))
            n.pure = c.pure and a.pure and b.pure and n.type is not None
            return n
        return c

    def binary(self, minp):
        left = self.unary()
        while True:
            op = self.peek()[1]
            p = BIN.get(op)
            if p is None or p < minp or self.peek()[0] != "p":
                return left
            self.take()
            right = self.binary(p + 1)
            if op in ("<", ">", "<=", ">=", "==", "!=", "&&", "||"):
                ty = INT if left.type and right.type else None
            elif op in ("<<", ">>"):
                ty = promote(left.type) if right.type else None
            else:
                ty = arith(left.type, right.type)
            n = Node("bin", op, [left, right], type_=ty)
            n.span = (left.span[0], self.i)
            n.pure = left.pure and right.pure and ty is not None
            left = n

    def is_type_start(self, k):
        tok = self.peek(k)
        return tok[0] == "id" and (tok[1] in TYPE_WORDS or tok[1] in QUALIFIERS)

    @spanned
    def unary(self):
        tok = self.peek()
        if tok[1] in ("-", "+", "~", "!") and tok[0] == "p":
            self.take()
            e = self.unary()
            ty = INT if tok[1] == "!" else promote(e.type)
            n = Node("un", tok[1], [e], type_=ty if e.type else None)
            n.pure = e.pure and n.type is not None
            return n
        if tok[1] in ("++", "--", "&") and tok[0] == "p":
            self.take()
            e = self.unary()
            return Node("opaque", kids=[e], pure=False)
        if tok[1] == "*" and tok[0] == "p":
            self.take()
            e = self.unary()
            if e.kind == "var" and e.value and e.value[0] == "ptr":
                return Node("load", text="*" + e.text, type_=e.value[1])
            return Node("opaque", kids=[e], pure=False)
        if tok[1] == "sizeof":
            self.take()
            if self.peek()[1] == "(":
                self.skip_parens()
            else:
                self.unary()
            return Node("opaque", pure=False)
        if tok[1] == "(" and self.is_type_start(1):
            j, words = 1, []
            while self.peek(j)[0] == "id":
                words.append(self.peek(j)[1])
                j += 1
            if self.peek(j)[1] == ")":
                self.i += j + 1
                e = self.unary()
                ty = parse_type(words)
                # the mapped spelling (log 204b: `(u32)` did not import)
                n = Node("cast", "(" + (ty[0] if ty else " ".join(words)) + ")", [e], type_=ty)
                n.pure = e.pure and ty is not None and e.type is not None
                return n
            self.skip_parens()  # pointer cast etc.
            self.unary()
            return Node("opaque", pure=False)
        return self.postfix(self.primary())

    def skip_parens(self):
        depth = 0
        while True:
            tok = self.take()
            if tok[0] == "end":
                raise SyntaxError("parens")
            if tok[1] == "(":
                depth += 1
            elif tok[1] == ")":
                depth -= 1
                if depth == 0:
                    return

    @spanned
    def primary(self):
        tok = self.take()
        kind, text = tok[0], tok[1]
        if kind == "num":
            m = tok[3]
            digits, suf = m.group(1), m.group(2).lower()
            if "l" in suf:
                return Node("opaque", pure=False)
            v = int(digits, 0) if not (digits.startswith("0") and len(digits) > 1 and digits[1] not in "xX") \
                else int(digits, 8)
            if v > 0xFFFFFFFF:
                return Node("opaque", pure=False)
            hexoct = digits[:1] == "0" and len(digits) > 1
            uns = "u" in suf or v > 0x7FFFFFFF and (hexoct or True)
            if v > 0x7FFFFFFF and not hexoct and "u" not in suf:
                return Node("opaque", pure=False)  # a decimal that large is long in C
            return Node("num", text=text, type_=UNS if uns else INT, value=v)
        if kind == "char":
            if tok[3] is None:
                return Node("opaque", pure=False)
            return Node("num", text=text, type_=INT, value=tok[3])
        if kind == "id":
            info = self.scope.get(text)
            if info is None:
                return Node("opaque", text=text, pure=False)
            if info[0] == "ptr":
                return Node("var", text=text, type_=None, value=info, pure=False)
            return Node("var", text=text, type_=info[1], value=info)
        if text == "(":
            e = self.expr()
            if self.take()[1] != ")":
                raise SyntaxError(")")
            return Node("paren", kids=[e], type_=e.type, pure=e.pure)
        if kind in ("str", "float"):
            return Node("opaque", pure=False)
        raise SyntaxError("primary " + text)

    def postfix(self, e):
        while True:
            first = e.span[0] if e.span else self.i
            e = self._postfix_once(e, first)
            if e is None:
                return self._last
            e.span = e.span or (first, self.i)

    def _postfix_once(self, e, first):
        self._last = e
        if True:
            tok = self.peek()
            if tok[1] == "[":
                self.take()
                idx = self.expr()
                if self.take()[1] != "]":
                    raise SyntaxError("]")
                if e.kind == "var" and e.value and e.value[0] == "ptr" and idx.pure and idx.type:
                    e = Node("load", text=e.text + "[" + render(idx) + "]", type_=e.value[1])
                else:
                    e = Node("opaque", kids=[e, idx], pure=False)
            elif tok[1] == "(":
                self.take()
                args = []
                if self.peek()[1] != ")":
                    args.append(self.assign())
                    while self.peek()[1] == ",":
                        self.take()
                        args.append(self.assign())
                if self.take()[1] != ")":
                    raise SyntaxError("call")
                e = Node("opaque", kids=args, pure=False)
            elif tok[1] in (".", "->"):
                self.take()
                self.take()
                e = Node("opaque", pure=False)
            elif tok[1] in ("++", "--"):
                self.take()
                e = Node("opaque", kids=[e], pure=False)
            else:
                return None
            return e


def render(n, names=None):
    """C text of a pure node; `names` maps input keys (var names / load texts) to parameter names."""
    if n.kind == "num":
        return n.text if not n.text.startswith("'") else str(n.value)
    if n.kind in ("var", "load"):
        return names[n.text] if names else n.text
    if n.kind == "paren":
        return "(" + render(n.kids[0], names) + ")"
    if n.kind == "un":
        return n.op + render(n.kids[0], names)
    if n.kind == "cast":
        return n.op + render(n.kids[0], names)
    if n.kind == "bin":
        return render(n.kids[0], names) + " " + n.op + " " + render(n.kids[1], names)
    if n.kind == "tern":
        return render(n.kids[0], names) + " ? " + render(n.kids[1], names) + " : " + render(n.kids[2], names)
    raise ValueError(n.kind)


def inputs(n, out):
    if n.kind in ("var", "load"):
        if n.text not in out:
            out[n.text] = n.type
    for k in n.kids:
        inputs(k, out)
    return out


def maximal_pure(n, out):
    """Maximal pure subtrees (skipping a bare paren wrapper)."""
    if n.pure and n.type is not None:
        while n.kind == "paren":
            n = n.kids[0]
        out.append(n)
        return
    for k in n.kids:
        maximal_pure(k, out)


def features(n, acc):
    if n.kind == "bin" and n.op in ("&&", "||"):
        acc.add("logic")
    if n.kind == "tern":
        acc.add("select")
    if n.kind == "bin" and n.op in ("<", ">", "<=", ">=", "==", "!="):
        acc.add("compare")
    if n.kind == "bin" and n.op in ("/", "%"):
        acc.add("divmod")
    if n.kind == "bin" and n.op == "*":
        acc.add("mul")
    if n.kind == "load":
        acc.add("load")
    for k in n.kids:
        features(k, acc)
    return acc


# --------------------------------------------------------------- functions

def blank_alternates(s):
    """Blanks every #elif/#else branch (to its #endif), so that the braces of
    `#if A  if(x){  #else  while(y){  #endif` count once (log 204b: SQLite's
    amalgamation lost every function after line 49,349 without this)."""
    out, stack = [], []  # stack: True while the current #if level is in an alternate branch
    for line in s.split("\n"):
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b", line)
        word = m.group(1) if m else None
        if word in ("if", "ifdef", "ifndef"):
            stack.append(False)
        elif word in ("elif", "else") and stack:
            stack[-1] = True
        elif word == "endif" and stack:
            stack.pop()
            out.append(line)
            continue
        out.append(re.sub(r"[^\n]", " ", line) if any(stack) and word is None else line)
    return "\n".join(out)


def functions(src):
    """(name, text, start offset) of every top-level function definition. Unlike
    c_census.functions, a `struct` in the parameter list does not exclude a
    function (log 204: the census skipped every function taking a struct)."""
    s = blank_alternates(c_census.strip(src))
    s = re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), s)
    res, depth, start, i, m = [], 0, 0, 0, None
    while i < len(s):
        c = s[i]
        if c == "{":
            if depth == 0:
                head = s[start:i]
                m = re.search(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", head)
                if m and (re.search(r"\btypedef\b|=", head) or m.group(1) in ("if", "while", "for", "switch")):
                    m = None
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if m:
                    first = start + len(head) - len(head.lstrip())
                    res.append((m.group(1), src[first:i + 1], first))
                start = i + 1
                m = None
        elif c == ";" and depth == 0:
            start = i + 1
        i += 1
    return res


def declarations(toks, scope):
    """Scan `type declarator, ...;` statements and parameter lists into scope (name -> ('val'|'ptr', type))."""
    i, n = 0, len(toks)
    clash = set()

    def add(name, info):
        if name in scope and scope[name] != info:
            clash.add(name)
        scope[name] = info

    while i < n:
        prev = toks[i - 1][1] if i else "{"
        if prev in ("{", ";", "}", "(", ",") and toks[i][0] == "id" and (toks[i][1] in TYPE_WORDS or toks[i][1] in QUALIFIERS):
            j, words = i, []
            while j < n and toks[j][0] == "id" and (toks[j][1] in TYPE_WORDS or toks[j][1] in QUALIFIERS):
                words.append(toks[j][1])
                j += 1
            ty = parse_type(words)
            # declarators until ; or ) at depth 0
            depth = 0
            while j < n:
                stars = 0
                while j < n and toks[j][1] == "*":
                    stars += 1
                    j += 1
                    while j < n and toks[j][1] in QUALIFIERS:
                        j += 1
                if j < n and toks[j][0] == "id" and toks[j][1] not in TYPE_WORDS:
                    name = toks[j][1]
                    j += 1
                    arr = j < n and toks[j][1] == "["
                    if ty is None:
                        add(name, ("bad", None))
                    elif stars == 1 and not arr or stars == 0 and arr:
                        add(name, ("ptr", ty))
                    elif stars == 0:
                        add(name, ("val", ty))
                    else:
                        add(name, ("bad", None))
                # skip to , or ; or ) at depth 0
                while j < n:
                    t = toks[j][1]
                    if t in "([{":
                        depth += 1
                    elif t in ")]}":
                        if depth == 0:
                            break
                        depth -= 1
                    elif depth == 0 and t in (",", ";"):
                        break
                    j += 1
                if j >= n or toks[j][1] != "," or prev == "(" and False:
                    break
                j += 1
                if prev == "(" or prev == ",":
                    # parameter list: each parameter has its own type
                    break
            i = j
        i += 1
    for name in clash:
        scope[name] = ("bad", None)
    return scope


def spans(toks):
    """(start, end) token index ranges of expression contexts in a function body."""
    out, n = [], len(toks)

    def close(j, open_, close_):
        depth = 0
        for k in range(j, n):
            if toks[k][1] == open_:
                depth += 1
            elif toks[k][1] == close_:
                depth -= 1
                if depth == 0:
                    return k
        return None

    for i, tok in enumerate(toks):
        if tok[0] == "id" and tok[1] in ("if", "while", "switch") and i + 1 < n and toks[i + 1][1] == "(":
            k = close(i + 1, "(", ")")
            if k:
                out.append((i + 2, k, tok[1]))
        elif tok[0] == "id" and tok[1] == "return":
            k = i + 1
            depth = 0
            while k < n and not (depth == 0 and toks[k][1] == ";"):
                depth += toks[k][1] in "([{"
                depth -= toks[k][1] in ")]}"
                k += 1
            if k > i + 1:
                out.append((i + 1, k, "return"))
        elif tok[0] == "p" and tok[1] in ASSIGN:
            k = i + 1
            depth = 0
            while k < n and not (depth == 0 and toks[k][1] in (";", ",", ")", "}")):
                depth += toks[k][1] in "([{"
                depth -= toks[k][1] in ")]}"
                if depth < 0:
                    break
                k += 1
            out.append((i + 1, k, "assign"))
    return out


def regions_of(name, text, min_ops, max_inputs):
    toks = tokenize(text)
    try:
        brace = next(i for i, t in enumerate(toks) if t[1] == "{")
    except StopIteration:
        return []
    scope = {}
    # parameters: the last (...) group before the body
    k = brace - 1
    while k >= 0 and toks[k][1] != ")":
        k -= 1
    depth, j = 0, k
    while j >= 0:
        depth += toks[j][1] == ")"
        depth -= toks[j][1] == "("
        if depth == 0:
            break
        j -= 1
    params = toks[j + 1:k]
    group = []
    for t in params + [("p", ",", 0, None)]:
        if t[1] == "," and group:
            declarations([("p", "(", 0, None)] + group + [("p", ")", 0, None)], scope)
            group = []
        elif t[1] != ",":
            group.append(t)
    body = toks[brace:]
    declarations(body, scope)
    found = []
    for s, e, ctx in spans(body):
        sub = body[s:e]
        if not sub:
            continue
        p = Parser(sub, scope)
        try:
            tree = p.expr()
            if p.i != len(sub):
                continue
        except (SyntaxError, IndexError, ValueError, RecursionError):
            continue
        cands = []
        maximal_pure(tree, cands)
        for r in cands:
            if r.ops < min_ops:
                continue
            ins = inputs(r, {})
            if not 1 <= len(ins) <= max_inputs or any(t is None for t in ins.values()):
                continue
            # Log 204b: character offsets in the function text of the region and
            # of each input's first occurrence (the text a splice passes).
            def chars(node):
                a, b = node.span
                return sub[a][4], sub[b - 1][4] + len(sub[b - 1][1])
            srcs = {}
            def collect(node):
                if node.kind in ("var", "load") and node.text not in srcs:
                    a, b = chars(node)
                    srcs[node.text] = text[a:b]
                for k in node.kids:
                    collect(k)
            collect(r)
            found.append((r, ins, ctx, sub[0][2], chars(r), srcs))
    return found


def emit(r, ins, origin):
    names, params = {}, []
    for k, (key, ty) in enumerate(ins.items()):
        pname = key if re.fullmatch(r"[A-Za-z_]\w*", key) and key != "region" else f"in{k}"
        names[key] = pname
        params.append(f"{ty[0]} {pname}")
    rt = promote(r.type)
    body = render(r, names)
    orig = render(r).replace("*/", "* /")
    return (f"/* {origin}: {orig} */\nstatic {rt[0]} region({', '.join(params)}) {{\n    return {body};\n}}\n",
            body, rt)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--keep", default=None)
    ap.add_argument("--tag", default="")
    ap.add_argument("--min-ops", type=int, default=3)
    ap.add_argument("--max-inputs", type=int, default=4)
    ap.add_argument("--algosearch", default="./algosearch")
    ap.add_argument("--json", default=None, help="log 204b: also write every region's file offsets and inputs")
    args = ap.parse_args()
    spans_out = []
    files = []
    for p in args.paths:
        if os.path.isdir(p):
            for root, _, fnames in os.walk(p):
                files += [os.path.join(root, f) for f in sorted(fnames) if f.endswith(".c")]
        else:
            files.append(p)
    if args.keep:
        os.makedirs(args.keep, exist_ok=True)
    nfun = nwith = total = imported = 0
    seen = {}
    feat = collections.Counter()
    with open(args.out, "w") as out, tempfile.TemporaryDirectory() as tmp:
        out.write("file\tline\tfunction\tcontext\tops\tinputs\tfeatures\timport\tunique\tid\ttext\n")
        for f in sorted(files):
            try:
                src = open(f, errors="replace").read()
            except OSError:
                continue
            for name, text, start in functions(src):
                nfun += 1
                base = src.count("\n", 0, start)
                regs = regions_of(name, text, args.min_ops, args.max_inputs)
                if regs:
                    nwith += 1
                for r, ins, ctx, line, (ca, cb), srcs in regs:
                    total += 1
                    origin = f"{f.split('scratchpad/')[-1]}:{base + line} in {name}"
                    code, body, rt = emit(r, ins, origin)
                    key = hashlib.sha1((rt[0] + "|" + ",".join(t[0] for t in ins.values()) + "|" + body)
                                       .encode()).hexdigest()[:12]
                    unique = key not in seen
                    fs = ",".join(sorted(features(r, set()))) or "arith"
                    status = seen.get(key)
                    if unique:
                        path = os.path.join(tmp, "r.c")
                        with open(path, "w") as g:
                            g.write(code)
                        res = subprocess.run([args.algosearch, "c-import", path], capture_output=True, text=True)
                        status = "OK" if res.returncode == 0 else c_census.reason(res.stderr or res.stdout)
                        seen[key] = status
                        if status == "OK":
                            imported += 1
                            feat.update(fs.split(","))
                            if args.keep:
                                with open(os.path.join(args.keep, f"{args.tag}{key}.c"), "w") as g:
                                    g.write(code)
                    if args.json is not None:
                        assert src[start + ca:start + cb].strip()
                        spans_out.append({"id": f"{args.tag}{key}", "file": f, "start": start + ca, "end": start + cb,
                                          "line": base + line, "function": name, "context": ctx, "ops": r.ops,
                                          "import": status, "rtype": rt[0], "body": body,
                                          "inputs": [{"key": k2, "src": srcs[k2], "type": t2[0]} for k2, t2 in ins.items()],
                                          "params": [p_.strip() for p_ in code.split("region(", 1)[1].split(")", 1)[0].split(",")]})
                    out.write(f"{f}\t{base + line}\t{name}\t{ctx}\t{r.ops}\t{len(ins)}\t{fs}\t{status}\t"
                              f"{int(unique)}\t{args.tag}{key}\t{render(r)}\n")
    if args.json is not None:
        import json
        with open(args.json, "w") as g:
            json.dump(spans_out, g, indent=1)
    print(f"{nfun} functions, {nwith} with a region; {total} regions, {len(seen)} unique, {imported} unique import",
          file=sys.stderr)
    print("features of unique imported regions:", dict(feat.most_common()), file=sys.stderr)


if __name__ == "__main__":
    main()
