# c-superoptimizer

**A source-level, algorithmic superoptimizer for C.** It takes a C function,
searches for a faster program that computes the same result, and writes it
back as C. The measure is the rewritten C compiled with `gcc -O2`, timed
against the original compiled with `gcc -O2`.

Instruction-level superoptimizers (Massalin, STOKE, Souper) work on short,
loop-free code, where `gcc -O2` is already strong. This tool searches over
loops, arrays and branches, so it targets what compilers do not rewrite:
the algorithm. Examples are a bit loop turned into SWAR, a counting loop
turned into a closed form, or an O(n²) loop turned into O(n).

It is built on [AlgoSearch](docs/ALGOSEARCH_README.md), a beam search over a
small imperative language ("Mini-C") that uses no machine learning.

> **Status: research prototype.** It works on small, self-contained functions.
> It does not import most real-world C (see [Limitations](#limitations)), and
> it has not yet produced an end-to-end gain on a real program.

## Example

The input is a popcount written as a loop over the 32 bits:

```c
int p_popcount_shift(int x) {
    int c = 0;
    for (int i = 0; i < 32; i++) c += (x >> i) & 1;
    return c;
}
```

The search reaches the classical SWAR popcount. Below is an excerpt of the
emitted C:

```c
v5 = (int)((unsigned)arr[0] - (unsigned)((arr[0] >> 1) & 0x55555555));
v5 = (int)((unsigned)(v5 & 0x33333333) + (unsigned)((v5 >> 2) & 0x33333333));
v5 = ((int)((unsigned)v5 + (unsigned)(v5 >> 4))) & 0x0F0F0F0F;
v5 = ((int)((unsigned)v5 * (unsigned)0x01010101)) >> 24;
```

The result was 11.6x faster than the original at `gcc -O2`, with 0
counterexamples on the tests (`--idiom-moves`, 60 s, seed 10).

## Results

Each number is the median over seeds 10–12, with 60 s of search per function
and compiled timing inside the search loop. The "control" column uses the
default configuration. The "idiom" column adds `--idiom-moves`. The held-out
functions were written after the moves and never used while developing them.

| function | control | idiom | written by hand |
|---|---|---|---|
| popcount, 32-bit loop | 1.0x | **11.6x** | 41x |
| parity, shift loop | 1.2x | **12.7x** | 39x |
| bit reversal, 32 bits | 1.2x | **8.2x** | 19x |
| next power of two | 1.0x | **7.5x** (proved on all 2³² inputs) | 9.8x |
| sum over a range | 1.45x | **553x** | ~9500x |
| *held-out:* division by 10 via subtraction | 1.0x | **36x** | 8x |
| *held-out:* popcount, 16 bits | 1.16x | **6.2x** | 14x |
| *held-out:* reversal, 16 bits | 1.21x | **6.0x** | 8.4x |
| *held-out:* sum below n | 1.42x | **529x** | ~10800x |
| *held-out control, no rule applies:* integer log2 | 1.06x | 1.0x | 2.9x |

The defaults alone (no idiom moves) also speed up 10 of 14 deliberately
de-optimized functions, by 1.3x to 14.7x (`tests/c_samples_deopt/`). On
functions taken from musl and Lua, idiom moves changed almost nothing: 1
seed pair got better and 3 got worse out of 63.

## Build

You need Linux on x86-64, gcc, make, and Python 3. Timing uses `rdtsc`.

```sh
make                 # builds ./algosearch and the ./c-superoptimizer command
make test            # builds and runs the unit tests
```

## Usage

```sh
# Superoptimize a function (the target is picked automatically, or use --func NAME)
./c-superoptimizer file.c --seconds 60 --time-top 4 --idiom-moves --out file.opt.c

# Only check whether a function is in the supported subset, and print its Mini-C
./c-superoptimizer import file.c --func NAME
```

`./algosearch superopt` and `./algosearch c-import` are the same commands.

Useful flags:

| flag | meaning |
|---|---|
| `--seconds S` | search budget (wall time) |
| `--seed N` | random seed |
| `--time-top K` | compile and time the best K candidates inside the loop (recommended: 4) |
| `--idiom-moves` | enable the rewrite moves: subtractive loop → `/` and `%`, Gauss sum, SWAR bit loops, bit smear, nested O(n²) array loop → O(n) |
| `--workload FILE` | time candidates on your own inputs instead of random ones |
| `--program-timer CMD` | time candidates inside your program |
| `--verbose` | print search progress |

The output file contains the rewritten C function with the same signature.
The log reports `measured_speedup` and an `equivalence_level`.

Batch runs and checks are in `scripts/`:

- `run_superopt_battery.sh` runs a set of samples over several seeds;
- `c_roundtrip.py` checks a candidate against the original, compiled;
- `c_speedup.py` measures the speedup;
- `c_exhaustive_check.py` proves a unary function on all 2³² inputs.

## Correctness guarantees

Every result reports how strongly it was checked:

| level | meaning |
|---|---|
| `exhaustive` | unary function, identical on all 2³² arguments (compiled) |
| `fuzzed` | array function: full workload, plus 200,000 mutated inputs |
| `tested` | counterexample-guided tests, including boundary values |
| `reference` | nothing faster was found, and the original is returned |

There is no SMT or bounded-model-checking proof yet. Treat `tested` results as
candidates to review, not as verified rewrites.

## Supported C subset

- One function that takes an `int`/`unsigned`/`char`/`short` scalar, or
  `int *a, int n`.
- Loops (`for`, `while`, `do`), `if`/`else`, `break`, `continue`, and integer
  arithmetic with C semantics (`gcc -fwrapv`, 32-bit `int`).
- Constant `#define`, `INT_MAX`/`INT_MIN`, char literals, and small inlined
  `return expr;` helpers.
- Pointers into the array parameter: `*p`, `p[i]`, `&a[i]`, `p++`, `p - q`,
  and pointer comparisons.
- Structs with only `int`/`unsigned` fields, through `struct S *s` or
  `struct S *p, int n`, with `->` and `.`.

## Limitations

- **Coverage of real code is small.** Only 0.13–1% of functions in real code
  bases import. The main blockers are typedef'd types, `#if` blocks, and
  structs with pointer fields.
- **Platform:** gcc on x86-64 only.
- **No end-to-end gain yet.** Gains on isolated functions or regions spliced
  into SQLite and libpng did not speed up the whole program, and winners
  found per region did not compose.
- **Equivalence relies on testing**, except for unary functions.

## Documentation

- [`docs/superopt.md`](docs/superopt.md) describes the design: semantics, the
  front end, the emitter, the search, timing, and the equivalence levels.
- `docs/findings/` and `FINDINGS.md` hold the full experiment log
  (sections 186–208), including negative results.
- [`docs/ALGOSEARCH_README.md`](docs/ALGOSEARCH_README.md) describes the
  underlying search framework.

## License

GPL-3.0. See [LICENSE](LICENSE).
