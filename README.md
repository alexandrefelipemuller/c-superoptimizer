# AlgoSearch

AlgoSearch is a C++20 program-synthesis framework that discovers algorithms
by beam search over a small imperative AST language ("Mini-C"), with **no
machine learning involved** — no neural network, no gradient descent, no
learned model of any kind. Candidate programs are generated from a
probabilistic grammar, executed against test instances by a tree-walking
interpreter, scored, and the best-scoring candidates survive to the next
round while the program is grown one node at a time.

The interesting part isn't "beam search finds programs" — it's the
experimental record of *why* it does or doesn't, kept in
[`FINDINGS.md`](FINDINGS.md) (summary; the full log is in `docs/findings/`). Read the summary before touching `src/search/`
or `src/language/` — it documents which levers (population diversity,
grammar bias, native operators) actually mattered and which didn't, with
pointers to the raw data backing each claim.

## How it works

1. **Mini-C**: a minimal language with scalar variables, one array, `if`,
   `while`, `swap`, native comparisons (`<`, `>`, `==`, `<=`, `>=`, `!=`),
   arithmetic (`+`, `-`, `*`, `/`, `%`), and bitwise operators (`&`, `|`,
   `^`, `<<`, `>>`) — the arithmetic/bitwise extension is recent (see
   `FINDINGS.md` section 17) and gated to an 8% substitution probability
   so it doesn't dilute the tuned grammar every earlier problem depends on.
2. **Generator**: builds random Mini-C programs from a grammar, optionally
   biased toward a canonical counting-loop idiom
   (`GenConfig::loopAccumulatorBias`) — see `FINDINGS.md` for why that
   bias exists and what it does and doesn't buy you.
3. **Interpreter**: executes a candidate against an instance (an input
   array) and returns the resulting array, with a step-count limit so a
   runaway/infinite loop just times out instead of hanging the search.
4. **Beam search**: scores every candidate against every training
   instance at the current difficulty level, keeps the best `beamWidth`
   programs, grows them, repeats. A level is only labeled a confirmed
   `SUCCESS` if the same unmutated program also holds several levels
   further ahead (`invarianceLookahead`) — see `FINDINGS.md` section 4.
5. **Benchmark / independent verification** (`--benchmark`): never trusts
   the search's own `SUCCESS` label at face value. Every candidate is
   re-run against held-out sizes, structurally adversarial instances
   (duplicates, negatives, sorted/reversed/all-equal), and a magnitude
   shift far outside the training range. Only candidates that survive
   this get reported, with an empirically fit time-complexity curve.

## Building and testing

```sh
make            # builds ./algosearch
make test       # builds and runs ./algosearch_tests (291 unit/integration tests)
```

## Running a search

```sh
./algosearch --problem gcd --max-n 20 --beam-width 500 --candidates 1500 \
             --max-program-size 150 --seed 1000 --benchmark --benchmark-seeds 5
```

Run `./algosearch --help` for the full flag list (search width/depth,
diversity/crossover/niching knobs, grammar-bias knobs, etc. — all of them
are either "confirmed default" or "opt-in, mixed evidence" per
`FINDINGS.md`).

## Importing C (superoptimizer prototype)

```sh
./algosearch c-import tests/c_samples/popcount_loop.c          # C -> Mini-C
./algosearch c-import tests/c_samples/popcount_loop.c --emit   # Mini-C -> C
scripts/c_roundtrip.py tests/c_samples/*.c                     # interpreter = original C = regenerated C
```

```sh
./algosearch --problem c:tests/c_samples/array_sum.c --max-n 30 --seed 2 --c-out found.c
scripts/c_roundtrip.py tests/c_samples/array_sum.c --candidate found.c
```

A subset of C (`int` scalars, one `int *a, int n` array) is translated into
Mini-C and back, and `--problem c:FILE.c` searches with that function as the
oracle and emits the solution as C. This is the first step towards using the
search as a source-level superoptimizer; no cost objective yet. Semantics, ABI,
supported subset and the plan are in `docs/superopt.md`.

## Results

All numbers are from the current defaults (see `FINDINGS.md`, section 2),
`--beam-width 500 --candidates 1500 --max-program-size 150 --step-limit
20000`, 180 s per run (240 s where `--num-vars` is set), `--benchmark
--benchmark-seeds 1`. A run counts as solved only if a program survives the
independent re-check (held-out sizes, adversarial and exhaustive inputs,
magnitude shift), not just the training score.

| benchmark | solved | source |
|---|---|---|
| 77-problem suite, 10 seeds each | **759 / 770** | `data/prefix_joint_battery_20261111` (base arm), section 178b |
| PSB2 integer subset (7 problems), 100 seeds, 900 s | 595 / 700 | sections 139-145 |
| external held-out set (3 problems from a public "easy array" list) | 4 / 30 with the defaults of the time; with `--local-cond-mutation` (now default) peak_element 15/30; search_rotated and equilibrium_point 10/10 only with the opt-in `--first-index --prefix-split`, built after diagnosing them | sections 114-117b |
| unseen PSB2 / PSB1-style problems (17), pieces on vs off | no difference beyond noise | sections 132-135 |
| learning curve on fresh problems (3 batches of 10, library frozen per batch) | 3, 6, 3 of 10 solved | sections 149-151 |

The suite overstates how far the system generalizes: it was written alongside
the pieces that solve it. On problems chosen from outside, most of what is
solved is what a piece was designed for.

### The classic problems, latest run

Solved out of 10 seeds, and the complexity `--benchmark` fits to the fastest
verified program (the most frequent fit across seeds). "Actual" is the
complexity of the algorithm found, read from the program, where the fit is
misleading.

| problem | solved | fitted | actual | note |
|---|---|---|---|---|
| `sorting` | 10 | O(n²) | O(n²) | selection/exchange sort; heap, merge and quick sort only with skeletons or the opt-in sorting moves |
| `min`, `max`, `sum`, `count`, `average` | 10 each | O(n) | O(n) | `min`/`max` also fit O(n²) in 3-4 seeds (a redundant inner loop) |
| `contains`, `contains_ordered` | 10 each | O(n) | O(n) | linear scan |
| `reverse` | 10 | O(n) | O(n) | two converging pointers |
| `single_number` | 10 | O(n) | O(n) | XOR reduce |
| `fibonacci`, `power` | 10 each | O(n) | O(n) | |
| `is_prime` | 10 | O(n) in 8, O(√n) in 2 | same | the default search now usually stops trial division at n, not √n |
| `gcd` | 10 | O(1) | **O(log n)** | Euclid with native `%`; the benchmark instance is gcd(n, 1), which ends after one step, so the fit cannot see the logarithm |
| `factorial` | 10 | O(1) | O(n) | the domain is n ≤ 12 (32-bit overflow), too narrow for a fit |
| `three_sum` | 7 | O(n²) | O(n³) | triple loop, fitted on n ≤ 8 |
| `graph_reachability` | 10 | O(n^1.5) | O(V³) | V rounds of matrix relaxation; n counts matrix cells |
| `lis`, `substring`, `cycle` | 10 each | O(n²), O(n²), O(n) | same | the old held-out rulers, now solved by generic pieces |

Per-problem results for all 77 problems: `python3 scripts/tally_battery.py
data/prefix_joint_battery_20261111`. Below 10/10 there are only
`majority_value` (3), `three_sum` (7) and `least_frequent_value` (9).

Results worth calling out:

- **Native arithmetic broke the complexity ceiling** (`FINDINGS.md` sections
  13, 17): before `Mul`/`Div`/`Mod`, every gcd and primality check was built
  from repeated subtraction, so work stayed linear. With native `%` the search
  finds Euclid's algorithm in every seed:
  ```
  v1 = arr[0]; v2 = arr[1];
  while (v2 != 0) {
    v3 = v1 % v2;
    v1 = v2;
    v2 = v3;
  }
  arr[0] = v1;
  ```
  It runs in O(log min(a, b)) steps (Lamé). A hand-timed A/B on gcd(n, 1)
  showed the exponent drop from 0.99 (subtraction) to ~0; that instance is the
  best case for Euclid, which is why the table shows O(1).
- **Skeletons were replaced by generic pieces** (sections 56-58, 99-117): of 15
  hand-written skeletons, 6 remain (`selectionsort`, `heapsort`,
  `binarysearch`, `mergesort`, `tspbruteforce`, `tspbranchandbound`), each the
  only route to its algorithm family. The rest were replaced by combinator
  pieces (scalar loop, triple aggregate, matrix relaxation, grid DP and others)
  that find the same problems at the same rate.
- **Sorting reaches five families only with opt-in moves** (sections 157-183):
  the default finds exchange/selection sort. Opt-in moves (`--adjacent-nested`,
  `--partition`, `--range-shell`, `--merge-shell`, `--prefix-combinator`) reach
  insertion, quick and merge sort, but cost 2-14 of 759 on the suite, so they
  stay a sorting profile, not a default.
- **`factorial`**'s training failures above n = 12 are 32-bit overflow, not
  search failures; the reference uses 64-bit arithmetic and treats overflow as
  out of domain.

See `FINDINGS.md` for the decisions behind each default, the closed negatives,
and pointers into `docs/findings/` for every number above.

## Held-out discovery benchmark (history)

Three problems deliberately **not** in the skeleton library now exist as the
ruler for open-ended discovery work: `lis`, `substring`, and `cycle`
(`src/problems/LisProblem.{hpp,cpp}`, `SubstringProblem.{hpp,cpp}`,
`CycleDetectionProblem.{hpp,cpp}`). The baseline
(`scripts/run_heldout_baseline.sh`, `data/heldout_baseline_20260920/`) froze
the starting line: 0/30 genuinely generalizing solutions across both arms
(default `loop-bias=0.4` and the `loop-bias=0.0` isolation setting) at a
matched modest budget. Etapa 2 added the opt-in divergence engine
(`--post-solve-sizes`, default 0) and ran it against the same ruler
(`scripts/run_heldout_divergence_ab.sh`): still 0/30 generalizing solutions
-- the window archives ~20-29 alternative gambiarras per run but creates no
new shape, confirming that the bottleneck is construction, not survival.
Etapa 3 added the opt-in composite-primitives operator (`--composite-bias`,
default 0.0): a two-loop family assembled from section 24's catalogued
primitives under one shared binding context. On the same ruler it COMPOSES
the target `substring` shape (champions contain the correct `n-m+1` bound
and alignment indices) but never converges to the clean version -- 0/15
generalizing in Test A, with `lis` (Test B) and `cycle` (reserve) out of
reach by construction, as designed.
See `FINDINGS.md` sections 25-27 for the protocol and the five-column taxonomy.
All three rulers are solved 10/10 by the current defaults (table above).

Generation-time pruning (`--prune-generation`, opt-in, off by default and
byte-identical to the unpruned grammar when off) redraws text-level trivia at
the moment it is generated: `v = v`, `arr[e] = arr[e]`, `swap(e, e)`, an
if/while condition that is constant by its own text, and a literal operand
that always errors (`/ 0`). Set `ALGOSEARCH_DEBUG_PRUNE_COUNTS=1` to print how
often each shape is drawn. A matched-seed A/B found it neutral: it removes
5-8% junk per site without changing success rate or wall time -- see
`FINDINGS.md` section 33.

The combinator algebra (`--combinator-bias`, opt-in) builds a block as a
product of layout x loop x relation axes from catalogued primitives under one
shared binding context. With no named skeleton available it solves sum, min,
max, count, contains, single_number, reverse, sorting (as Fung's "I can't
believe it can sort") and substring, plus three problems written after its
axes were frozen (`palindrome`, `inversion_count`, `has_duplicate`); it
cannot reach `is_sorted`, `cycle` or `lis`, as predicted. `--benchmark` now
also checks every small input for problems that define
`Problem::exhaustiveInputs`. See `FINDINGS.md` sections 38-39.
