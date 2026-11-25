#!/usr/bin/env python3
"""Log 211: count length-changing-prefix (LCP) instructions in a compiled C function.

A 16-bit operation with a 16-bit immediate (`andw $28672, %ax`) carries a 0x66
operand-size prefix that changes the instruction length; Intel predecoders
stall a few cycles on each one. Code over uint16_t/short can be several times
slower in a decode-bound loop for this reason alone (log 211: the original
key_type_is_raw_bytes became 7x faster when only its 4 such instructions were
widened). A winner whose original has LCP instructions and which has none may
owe its in-loop gain to this, not to its algorithm.

    scripts/c_lcp_check.py FILE.c FUNC   -> prints `lcp N` (gcc -O2, x86-64)
"""
import re
import subprocess
import sys


def lcp_count(path, func):
    text = open(path).read()
    text = re.sub(r"\bstatic\s+(?:inline\s+)?((?:[A-Za-z_]\w*\s+)+\**\s*%s\s*\()" % re.escape(func), r"\1", text)
    r = subprocess.run(["gcc", "-O2", "-fwrapv", "-w", "-x", "c", "-S", "-o", "-", "-"], input=text,
                       capture_output=True, text=True)
    if r.returncode:
        return None
    body = re.search(r"(?ms)^%s:\n(.*?)\.cfi_endproc" % re.escape(func), r.stdout)
    if not body:
        return None
    n = 0
    for m in re.finditer(r"^\s+([a-z]+)w\s+\$(-?\d+),", body.group(1), re.M):
        if m.group(1) in ("mov", "and", "or", "xor", "add", "sub", "cmp", "test", "adc", "sbb", "imul") \
                and not -128 <= int(m.group(2)) <= 127:
            n += 1
    return n


if __name__ == "__main__":
    print("lcp", lcp_count(sys.argv[1], sys.argv[2]))
