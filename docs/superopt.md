# C superoptimizer prototype

Long-term goal: use AlgoSearch as a source-level, *algorithmic* superoptimizer
for C. Instruction-level superoptimizers (Massalin, STOKE, Souper) work on
loop-free code, where gcc -O2 is already strong. AlgoSearch searches over
loops, arrays and recursion, so the place where it could win is rewriting the
algorithm: a loop into a closed form or a bit trick, O(n^2) into O(n), a branch
removed. That claim can be falsified: the measure is the optimized C, compiled
at -O2, against the original compiled at -O2.

Interface gaps (planned 2026-11-13), and their status:

| # | gap | status |
|---|---|---|
| 0 | interpreter semantics match C (`gcc -fwrapv`, 32-bit `int`) | done (log 186) |
| 1 | C -> Mini-C front end, Mini-C -> C emitter, round-trip check | done (log 186) |
| 2 | `ReferenceProblem`: the original function is the oracle | done (log 187) |
| 3 | weighted cost model calibrated on compiled code; search seeded with the reference, objective cost given correctness | next |
| 4 | equivalence beyond tests (compiled differential testing at scale; later SMT/BMC) | |
| 5 | `algosearch superopt FILE --func f`: optimized C + measured speedup + level of the equivalence guarantee | |

## Semantics (phase 0)

The model is C compiled by gcc with `-fwrapv` on 32-bit `int`:

- `+`, `-` wrap (two's complement) in every mode. Before this change the
  interpreter computed them with plain signed `int` arithmetic, which is
  undefined on overflow; the values are the same, the undefined behaviour is gone.
- `*` wraps only with `Interpreter::setWrapArithmetic(true)`; by default an
  overflowing multiply is still a runtime error, so every search is unchanged.
- Division or modulo by zero, `INT_MIN / -1`, shifts outside [0, 31] and
  out-of-bounds array accesses are runtime errors in both modes. In C they
  trap or are undefined, so an input on which the reference errors is outside
  the function's domain.
- `>>` is arithmetic (as gcc does), `<<` operates on the bits (as gcc
  `-fwrapv` does).

## ABI

| C signature | Mini-C input `arr` | `v0` | result |
|---|---|---|---|
| `int f(int a, int b, ...)` | `[a, b, ...]` | arity | `arr[0]` |
| `int f(int *a, int n)` | `a` | `n` | `arr[0]` (needs n >= 1) |
| `void f(int *a, int n)` | `a` | `n` | the whole final array |

Scalar arguments are copied into v1, v2, ... by a prologue. `return e` becomes
`arr[0] = e; return;`. When the function writes its length parameter it gets a
private variable, so `v0` is always the length register the search expects.

## Front end (`src/superopt/CFrontend.cpp`)

Supported: `int`/`int32_t`/`const int` scalars and locals, one array
parameter `int *a` / `int a[]` plus its length, `if`/`else`, `while`,
`do`/`while`, `for`, `break`, `continue` (not inside a `for` with a step or a
`do`/`while` yet), `return`, `=`, compound assignment, `++`/`--` as
statements, every Mini-C operator, unary `-`, `~`, `!`, casts to `int`, and
`&&`, `||`, `?:`. Short circuit is preserved: when the right operand can fail
(array read, `/`, `%`, shift) it is evaluated in a guarded temporary, so
`i < n && a[i] > 0` never reads `a[n]`. Anything else (other types, pointers,
calls, assignments inside expressions, macros, local arrays) throws
`CImportError` with the line; nothing is approximated.

## Emitter

`emitCFunction` produces C with the original signature. In an int function,
`arr[0] = E; return;` becomes `return E;`; if the program writes the array
anywhere else, the generated code works on a local copy, so the caller's array
is never changed by an int function. The output is equivalent to the Mini-C
program on every input where that program completes without a runtime error.
`call()` (recursion) cannot be emitted yet.

## Reference problem (phase 2)

`--problem c:FILE.c` (or `c:FILE.c:func`) searches against a C function:
`ReferenceProblem` imports it and uses it as the oracle, so the expected
result of any input is what the reference computes in wraparound mode.

- Domain: an input on which the reference has a runtime error, or runs past
  10M interpreter steps, is outside the function's domain. It never becomes an
  instance and `isSolution` accepts any output on it. Note the second case: a
  slow reference narrows its own domain at large magnitudes.
- Levels: the array length for array functions (as in the array problems).
  For scalar functions, every argument is drawn from [-M, M] with
  M = 2^(L+1) - 1, exhaustively while the grid has at most 81 points, then
  values near the bounds, powers of two (each at least once) and uniform
  samples. From L = 30 on, M covers all of int and INT_MIN is included. Use
  `--max-n 30` to reach the full range.
- Verification (`--benchmark`, adversarial instances): arrays over [-2, 2]
  up to n = 5 exhaustively; scalar functions on a grid around 0 (|x| <= 1024
  for one argument, <= 64 for two, ...), and edge values over all of int.
- Arithmetic: `Problem::wrapArithmetic()` returns true here, so the search's
  and the verification's interpreters run in wraparound mode. Every other
  problem keeps the old semantics (byte-identical, log 186-187).
- Partial credit: positions already right, for void array functions only.
- After the search, the confirmed (SUCCESS) solution that reached the highest
  level, the smallest among those, is printed as C and written with `--c-out`.

## Tools

    ./algosearch c-import FILE.c [--func NAME]      # Mini-C translation
    ./algosearch c-import FILE.c --emit             # C regenerated from it
    ./algosearch c-import FILE.c --eval INPUTS      # interpreter result per input line
    ./algosearch c-import FILE.c --program P --emit # C for another Mini-C program under FILE's ABI
    scripts/c_roundtrip.py tests/c_samples/*.c      # interpreter = original C = regenerated C
    scripts/c_roundtrip.py FILE.c --candidate X.c   # a search result against the original

    ./algosearch --problem c:tests/c_samples/array_sum.c --max-n 30 --beam-width 500 \
        --candidates 1500 --max-program-size 150 --seed 2 --c-out found.c

`c_roundtrip.py` compiles the original and the regenerated function with
`gcc -O2 -fwrapv`, runs random and edge-case inputs through the interpreter and
both binaries, and fails on any difference, on a native run longer than
`--native-timeout`, or when fewer than `--min-compared` of the inputs complete
in the interpreter (a translation that errors everywhere would otherwise pass
by comparing nothing).
