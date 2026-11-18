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
| 3 | cost model; search from the reference, objective cost given correctness; compiled speedup measurement | done (log 188-188b): one real gain; the uncalibrated model does not predict compiled time |
| 3b | compiled timing of candidates inside the loop (`--time-top K`) | done (log 189) |
| 3c | reach: algorithmic moves (default; `--no-algorithmic-moves`), a second, larger sample set | done (log 190, 190b): moves are the default; gains on 7 of 23 samples vs 4 without; timer on 512 fresh inputs. Reach moves + structural timing (203, default): memsys3Roundup gap closed, 12 vs 9 gains on 36 samples |
| 4 | larger functions and real code, in small steps. 4a: constant `#define`, `INT_MAX`/`INT_MIN`, inlined `return expr;` helpers, `continue` in any loop, target picked automatically | 4a done (log 191): 8 real-style samples import exactly; 1 gain of 6 measurable; no gain returns the original text |
| 4b | real code: census of musl/Lua functions that import; superopt on them | done (log 192/192b): under 1% import; is_leap 2.7-4.0x proved. Char literals (193), `unsigned` (194), `char`/`short` (197); ceiling study + 65,536-input timer (198/198b); string pilot (199); 6 more code bases, 0.19% import, byte-buffer timing context (200); in-loop byte context, opt-in (201); cross-context confirmation, opt-in (202); scalar regions inside functions: 1,448 importable in 8 code bases, 18/40 gain on random inputs (204); spliced back into SQLite: no end-to-end gain, hot regions < 1% of its time (204b) |
| 5 | equivalence beyond tests (compiled differential testing at scale; later SMT/BMC) | started (log 192): boundary inputs in CEGIS; unary total functions checked on all 2^32 arguments compiled; array functions: whole workload + workload mutations in CEGIS, 200,000-mutation final check, level `fuzzed` (199/199c) |
| 6 | `algosearch superopt FILE --func f`: optimized C + measured speedup + level of the equivalence guarantee | |

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
`do`/`while`, `for`, `break`, `continue`, `return`, `=`, compound assignment,
`++`/`--` as statements, every Mini-C operator, unary `-`, `~`, `!`, casts to
`int`, and `&&`, `||`, `?:`. Short circuit is preserved: when the right operand
can fail (array read, `/`, `%`, shift) it is evaluated in a guarded temporary,
so `i < n && a[i] > 0` never reads `a[n]`.

Since phase 4a (log 191):

- Object-like `#define NAME body` is expanded at lexing. `INT_MAX` and
  `INT_MIN` are predefined, and `#include` lines are ignored.
- A call to another function of the file whose body is `return expr;` is
  inlined. Each argument is evaluated once, in order, into a temporary, under
  the same guard as the call.
- `continue` in a `for` with a step or a `do`/`while` translates as
  `while (1) { B; break; }`. Inside it, `continue` is the inner break and
  `break` sets a flag that leaves the outer loop.
- Without `--func`, the target is the file's only function that no other
  function calls.
- Character literals (log 193) are int constants: one ASCII character or
  escape (`\n`, `\0`, `\'`, octal, `\xHH`). Values above 127 are rejected,
  since their value depends on whether `char` is signed.
- `char`, `short`, signed or unsigned, and the 8/16-bit <stdint.h> names
  (log 197): values promote to int in expressions and are truncated on
  storage (variables, parameters, results, casts). Pointers to them
  (`char *`) are still rejected. `c-import` prints `narrow=1`, and the
  scripts then call the function through a typed wrapper.
- 32-bit `unsigned` (log 194): `unsigned`, `unsigned int`, `uint32_t` and
  casts between them and int, typed by C's usual arithmetic conversions. The
  Mini-C value is the int with the same bits. Ordered comparisons become
  `(a ^ INT_MIN) < (b ^ INT_MIN)`, and an unsigned `>>` becomes a masked
  arithmetic shift. A `u` suffix and hex/octal literals above `INT_MAX` are
  unsigned, as in C. Unsigned `/` and `%` (log 196) use signed division only
  (Hacker's Delight 9-3: shift or mask for powers of two, a compare for
  divisors >= 2^31, else halve, divide and correct). `unsigned
  char/short/long` and `L` suffixes are still rejected. The emitter keeps the
  original signature.

Anything else throws `CImportError` with the line; nothing is approximated.
That covers other types, pointers, function-like macros and `#if`, helpers
with more than a `return`, recursive helpers, calls as statements,
assignments inside expressions and local arrays.

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

## Superoptimizer (phase 3)

`algosearch superopt FILE.c [--seconds 30] [--seed 1] [--out FOUND.c]` starts
from the imported function and searches for an equivalent, cheaper one.

- Cost: `interpreter/CostModel.hpp`, a weighted dynamic count of the nodes
  executed (`Interpreter::setCostModel`; off by default and byte-identical):
  simple ALU ops, loads and stores 1, multiply 3, divide/modulo 20, a branch 1
  (every `if`, every loop-condition check), variables free. For scalar
  functions, `arr[constant]` is free (gcc keeps the argument array in
  registers). The weights are uncalibrated guesses.
- Workload for the cost: 64 scalar inputs (70% over all of int) or 24
  arrays of 16-64 values, all inside the reference's domain.
- Correct means passing every test: all levels of the ReferenceProblem plus
  adversarial instances. Each run gets a step budget of 4x the reference's
  steps on that input plus 200, so a mutant stuck in a loop is cheap to reject.
  The reference's domain is the inputs it finishes within 1M steps.
- Moves: delete a statement; replace, hoist or copy an expression; change an
  operator; tweak a constant; replace an `if` or a loop by its body; insert an
  assignment; swap adjacent statements. One to four per mutant.
- Beams: correct programs by (cost, size), plus an exploration beam of
  candidates failing at most 10% of the tests (STOKE-like, so the search can
  cross incorrect intermediates).
- CEGIS: each new best is checked on the exhaustive inputs plus 2000 random
  ones; a counterexample becomes a test and the beam is re-filtered. The
  result also passes `verifyGeneralization`. The process exits with 3 if it
  does not.

Results so far:
- With the model alone (log 188b): all 45 results were compiled-equivalent,
  with one real gain: `is_pow2_loop` at 1.15-1.34x (the search removes two
  zero bits per iteration). But the model's ranking barely matched compiled
  time (Spearman 0.12), and its favourite for `count_trailing_zeros` ran at
  0.55-0.60x.
- With `--time-top 4` (log 189, the recommended mode): 45/45 equivalent and
  no regression (lowest 0.963). `is_pow2_loop` gains 1.17-1.20x, and `sign`
  gains 1.64x with a rewrite the model rates as equal cost
  (`(x > -1) - (x <= 0)`, a gcc code-generation effect). When nothing beats
  the noise, the reference is returned (32 of 45 runs).

### Compiled timing in the loop (phase 3b)

`--time-top K` (`src/superopt/CompiledTimer.cpp`) compiles each timed
candidate together with the original into one timing translation unit and
times them interleaved in one process, so both can be inlined into their
timing loops. A separate-executable harness carried a process-level bias of
up to 1.5x (log 188b).

- At the start, the original is timed against itself 3 times. The noise
  threshold is max(3%, 2x the largest deviation from 1).
- In each generation, up to K untimed correct candidates are timed: the
  model's best, mutants of measured-fast parents, then random ones. Each first
  passes the counterexample search. Timing may take at most half of the wall
  clock so far.
- The fastest timed programs become parents, so measured time steers the
  search while the model still ranks the beam.
- At the end, the three fastest are re-measured (3 x 21 trials). The result
  is the fastest one above the noise threshold, or the reference itself
  ("no measured gain"), never a program the model likes and the machine does
  not.

### Algorithmic moves (phase 3c)

The algorithmic moves (default since log 190b; `--no-algorithmic-moves` turns them off) turn a quarter of the edits into moves that change the
shape of the algorithm rather than one node:

- unroll a loop twice: `while (c) { B; if (!c) break; B }` (exact);
- `if (c) v = a; [else v = b;]` becomes the branch-free select
  `v = b ^ ((a ^ b) & (0 - c))`;
- insert an early exit: `if (c) break;` in a loop, or `if (c) return E;`;
- a loop becomes an `if` (one iteration);
- strength reduction by powers of two (`*` to `<<`, `/` to `>>`, `%` to `&`),
  valid only for non-negative operands, which the tests decide;
- (log 203, `--reach-moves`, default; `--no-reach-moves` turns it off) branch
  merge: `if (c) R = K; else R = E(x)` becomes `if (c) x = a; R = E(x)`, with
  `a` K or a constant of c +- 1 (memsys3Roundup's clamp);
- (log 203) divisibility split, exact: `x % (2^p b) == 0` becomes
  `(x % 2^p == 0) & (x % b == 0)` (`!=`: `|`).

With `--reach-moves`, one correct mutant made by a phase 3c move is also timed
per generation, before the beam is cut. The model cost does not see
mispredictions, so a branch replaced by a clamp can rank below the beam's
equivalents and never be timed: memsys3Roundup's clamp (3.96x) was produced
and dropped until this was added.

Without the flag, the edits and the random stream are exactly those of phase
3. `tests/c_samples_set2/` holds 8 larger samples with known better forms
(max_index, sum_abs, count_positive, popcount_naive, reverse_bits8,
count_digits, clamp, mul_by_add). gcc already turns `mul_by_add`'s loop into a
multiply, and `clamp` is below timing resolution.

The in-loop timer runs on its own fresh inputs (`timingInputs`; 512 until log 198), the size
and distribution of the external measurement. In log 190 it reused the 24
cost-model inputs. Repeated, their branches were memorized by the branch
predictor, and two branchy candidates timed as 1.24x and 1.42x in the loop
but ran at 0.34x and 0.92x on fresh data.

512 is not enough either (log 198). Measured on 65,536 inputs that do not
repeat within a pass, musl's `is_leap` costs 5.8 ns per call, against 1.9 ns
on 512 repeated inputs. A zeroinfnan winner proved equivalent and timed at
1.5x on 512 runs at 0.18x. filterpc's branch-free form, 0.54x on 512, runs at
5.9x, and two 197b isspace winners measured 3.7x/2.8x on 1024 held-out bytes
run at 0.39x/0.41x on 65,536 bytes of text. The in-loop timer and
`c_speedup.py` therefore use one long pass: 65,536 scalar inputs (16,384
arrays), and a shorter `--workload` / `--inputs-file` is resampled in a random
order to that size (its distribution, not a sequence the predictor can learn).

The calling context matters too (log 200). The timer passes each argument
from an int array; for a `char` parameter gcc then inlines different code for
versions that compile to the same function (is_cr_or_lf: identical assembly,
1.41x vs 1.00x). `scripts/c_byte_caller.py` times character functions in a
loop over a `const char *` buffer, as real callers do (aligned since log 201:
unaligned, identical copies measured up to 1.27x apart): isalpha's 1.8x there
is 1.36x, ascii_is_unsafe's 3.6x is 2.3x. `--timing-context byte` (log 201,
opt-in) makes the in-loop timer read a byte-input unary function's argument
from a byte buffer. It found the same gains as the int context and accepted
one regression (hex_to_int 1.15x in the loop, 0.88x in c_byte_caller): two
byte-buffer callers differ by up to 30% on branchy code, so a gain under about
1.2x on a byte function depends on the caller. The default stays `int`.

`scripts/c_speedup.py ORIGINAL.c FOUND.c` measures what counts: both compiled
with gcc -O2 -fwrapv in separate translation units, called through a volatile
function pointer, with the empty-call overhead subtracted. It takes the
median of 15 interleaved trials, measured with both link orders (geometric
mean). Functions and loops are aligned to 64 bytes, because code placement
alone moved one loop by 25%. Self-controls (a function against itself) stay
within 0.98-1.01. Below about 2 ns per call, timing through a call is not
readable. `--inline` instead compiles each function into the timing loop's own
translation unit, which is how a caller would use it. Even then, a ~1 ns array
function measured up to 1.5x against itself, so every claim keeps a
self-control of the same function.

### Workload (log 195)

By default the compiled timing uses 65,536 random inputs (log 198): uniform ints, 30% of
them small. For a function whose real inputs are narrower, such as characters
for ctype, give them with `--workload FILE`, one input per line as for
`c-import --eval`. The in-domain inputs replace the random ones in the timing
and the model cost, and the first 64 also become tests. Correctness is still
checked on all inputs, and the counterexample search scans the whole workload
(log 199). For string code (int arrays holding NUL-terminated text) that is not
enough: random arrays rarely hold a NUL, so almost nothing outside the workload
is checked, and all three strcasecmp winners of log 199 were wrong on other
bytes (`(a | 32) == (b | 32)` treats `[` and `{` as equal). Since log 199c
an array function with a workload also gets mutations of workload inputs
(bit flips, near-equal aligned elements, zeros, program constants) in CEGIS,
and a timed winner must pass 200,000 of them (`equivalence_level fuzzed`,
`fuzz_rejected` counts the refuted ones). `tested` still means unverified
beyond the tests. The result is tuned to that workload by design: on
text, isspace gains 2.3-2.5x with `if (c >= 33) return 0;` first, which
would cost on whitespace-heavy input. Without a workload, character functions
picked candidates that regressed on characters, down to 0.23x (hexval).

Confirmation (log 197): the search times its candidates on one fixed input
sequence, and the fastest of hundreds can be fast on that sequence only (a
branch the predictor happened to learn: 1.53x there, 0.57x on other inputs
of the same kind). So the workload is split into alternate inputs. The loop
times on one half, and the final top three are re-measured on the other half
before one is accepted. Without `--workload` the second half is a second
random set.

### Equivalence levels (log 192)

`superopt` prints `equivalence_level`:

- `reference`: nothing changed; the original text is returned.
- `exhaustive`: a unary function. The accepted program was compiled and
  compared with the original on all 2^32 arguments (about 5 s, limit 60 s).
  Where the reference may fail, a mismatch counts only on an argument the
  reference defines (log 194), because the compiled values elsewhere are
  undefined. A timed winner that this check refutes is skipped
  (`exhaustive_rejected`). A check that times out or crashes leaves the
  level at `tested`.
- `tested`: tests, CEGIS and boundary inputs only. Boundary inputs are the
  reference's constants (constant subexpressions folded) +-2 in each argument,
  plus the whole tail for a constant within 4096 of an int limit.

Why it matters: on musl's `is_leap`, three test-equivalent candidates dropped
the `y > INT_MAX - 1900` overflow guard and were wrong on 4 to 29 arguments.
Inputs the reference fails on are outside the domain everywhere, including
`verifyGeneralization` (`Problem::definesAnswer`).

## Tools

    ./algosearch c-import FILE.c [--func NAME]      # Mini-C translation
    ./algosearch c-import FILE.c --emit             # C regenerated from it
    ./algosearch c-import FILE.c --eval INPUTS      # interpreter result per input line
    ./algosearch c-import FILE.c --program P --emit # C for another Mini-C program under FILE's ABI
    scripts/c_roundtrip.py tests/c_samples/*.c      # interpreter = original C = regenerated C
    scripts/c_roundtrip.py FILE.c --candidate X.c   # a search result against the original
    scripts/c_roundtrip.py FILE.c --candidate X.c --inputs-file W.txt   # on given inputs, e.g. strings (log 199)
    scripts/c_census.py --out T.tsv --keep DIR SRC/ # which functions of a code base import on their own
    ./algosearch superopt FILE.c --seconds 60 --out X.c  # phase 3
    scripts/c_speedup.py FILE.c X.c                  # compiled speedup
    scripts/c_speedup.py FILE.c X.c --inline --inputs-file W.txt   # on a given workload
    ./algosearch superopt FILE.c --time-top 4 --workload W.txt     # search timed on your inputs (log 195)
    scripts/run_ceiling_battery.sh OUT TAG          # hand versions vs search winners (log 198; RUNS=DIR for other winners, 198b; DIR=, LIST= for another sample set, 200)
    scripts/c_byte_caller.py W.txt ORIG.c X.c ...    # char functions timed in a loop over a byte buffer (log 200; aligned, 201)
    scripts/c_regions.py --out R.tsv --keep DIR SRC  # pure scalar expressions inside real functions, as region() files (log 204; --json offsets, 204b)
    scripts/c_regions_splice.py instrument|splice ... # record a region's inputs in its program / splice replacements in (log 204b)
    scripts/make_int_workloads.py OUTDIR [--large N] # nibble / allocation-size workloads (log 200)
    # a speedup ratio is comparable only within one session (198b): re-measure the baseline arm alongside

    ./algosearch --problem c:tests/c_samples/array_sum.c --max-n 30 --beam-width 500 \
        --candidates 1500 --max-program-size 150 --seed 2 --c-out found.c

`c_roundtrip.py` compiles the original and the regenerated function with
`gcc -O2 -fwrapv`, runs random and edge-case inputs through the interpreter and
both binaries, and fails on any difference, on a native run longer than
`--native-timeout`, or when fewer than `--min-compared` of the inputs complete
in the interpreter (a translation that errors everywhere would otherwise pass
by comparing nothing).

## Emitted C and -fwrapv (log 204b)

Mini-C arithmetic wraps. Until log 204b the emitted C used plain `+ - *` on
`int` and said "compile with -fwrapv"; every timer and checker did. Spliced
into SQLite (built without -fwrapv), a winner correct under wrapping, `(iEnd
+ 4) - (-2147483647) > v1 + 2147483647`, was simplified by gcc as if
overflow could not happen and corrupted the database. `emitCFunction` now
writes `+ - *` through `unsigned`, so emitted code is valid C under any
flags.
