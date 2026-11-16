# Findings summary

The full log (222 numbered section headers, 526 KB) is in `docs/findings/`, split into 11
chunk files in the order it was written. Nothing was deleted: the chunks
concatenate to the old `FINDINGS.md` byte for byte (git tag `pre-summary`).
**Read this file first; open a chunk only for the section it points to.**

    grep -n '^| 104' docs/findings/INDEX.md      # which chunk and line holds section 104
    sed -n '1,80p' docs/findings/09_sections_100-111.md

Section numbers below refer to that log. Code comments that say "FINDINGS.md
section N" mean the same numbers. When you add a section, append it to the
last chunk and add one line here (state, decision table or lessons).

## 1. What the tool is

C++20 program synthesis: beam search over a small imperative language
("Mini-C": int variables, `arr`, an `aux` array, `swap`, `while`, `if`,
`call(lo, hi)` recursion), no machine learning. A problem supplies instances
and `isSolution`. A program is promoted level by level (N = 1, 2, ...); a
final check re-verifies it outside the search (`--benchmark`, adversarial and
exhaustive inputs). Selection is by exact solved count; a candidate pool of
mutated, fresh and skeleton-seeded programs feeds the beam.

## 2. Current defaults and how they got there

Generic pieces that are on by default (each was promoted by a matched-seed A/B
and, from section 99 on, a joint battery with a no-drop-of-2 rule):

| default | section | note |
|---|---|---|
| combinator algebra (product of catalogued axes) | 38, 47, 48 | the core generic mechanism |
| adjacent-pair axis | 40, 42 | |
| step axes (`--combinator-step`) | 52, 53 | |
| `--tail-enum 10` (enumerate the small tail after a block) | 51, 54 | |
| stateful pieces (Kadane, rain water, LIS) | 55, 61, 62 | |
| nested aggregates | 71, 72 | |
| frequency table | 75, 76 | |
| pointer-following piece, gated on pointer-like inputs | 77-79, 90 | ungated version cost max_gap_sorted (78) |
| orthogonal modifiers (direction, abs) | 80, 81 | |
| arg-best | 82-85 | |
| distinct counting + regression guard 12 | 86-89 | the guard removed the n = 1 rejections |
| `--confirm-reseed 12` | 97 | fresh-seed confirmation against false positives |
| scalar-loop piece (+ index range) | 100, 101 | replaced four scalar skeletons and `average` |
| triple aggregate | 103 | replaced `threesum` |
| matrix relaxation | 104, 105 | replaced `graphreachability`; `shortestpath` never worked |
| grid DP | 109 | replaced `dtw` |
| per-term transform | 111 | |
| distinct aggregate (max on DistinctCount) | 112b-112d | the first version cost max_repeated_value (a deceptive attractor) |
| arg sentinel guards | 113 | |
| `--local-cond-mutation 0.3` | 115, 115b | 44 vs 21 of 150 |

Hand-written skeletons still in the library (all shown indispensable or
undecidable in section 99/102/107/108): `selectionsort` (102b, closed),
`heapsort`, `binarysearch` and `mergesort` (inert in the default, 108),
`tspbruteforce`, `tspbranchandbound` (107b-107d). Removed along the way:
`counting`, `divideconquer` (57), four scalar skeletons (100), `average` (101),
`threesum` (103), `graphreachability` (104), `shortestpath` (105), `dtw` (109).
`ALGOSEARCH_DISABLE_SKELETON=name` turns one off for an ablation.

Reference numbers: joint battery 717 of 730 solved (113); external held-out
set from a public list 4 of 30 (114), i.e. the battery overstates how far the
system generalizes.

## 3. Opt-in, kept

| flag | section | why it is not default |
|---|---|---|
| `--profile-novelty W`, `--profile-ban` | 98, 118 | compresses the archive ~100x, finds heap sort in 8/8 seeds, but no new family; the ban adds <= 2 classes per seed |
| `--post-solve-sizes N`, `--restart-on-stall K`, `--wall-seconds S` | 92-96 | restarts multiply variants, not families |
| `--first-index`, `--prefix-split` | 116, 117, 117b | close two gaps; PrefixSplit costs frequent_mode_or_none |
| `--combinator-follow` (ungated), `--combinator-v1` | 77-79 | ablation |
| `--recursion-call-bias`, `--second-array-bias` | -- | recursion seeding (`v1/v2 = 0/n`) and the `aux` array; off unless a problem needs them |
| `--dc-shell-search` (+`-key`, `-end`) | 121-124 | assembles O(log n) search on three specifications; costs nothing on the 73-problem battery; no measured gain in solved counts, so not promoted |
| `--max-executions N` | 126 | execution cap in PSB2 units; `Psb2Problem` + `scripts/run_psb2.sh` run seven integer PSB2 problems under the suite's protocol |
| `--term-expr P` | 126b, 127, 129, 130 | enumerated `x op c1 op c2` term in loop sums; fuel_cost 0/30 -> 29/30 (paired_digits 4 -> 2); DEFAULT 0.3 since 130 |
| `--prefix-cross P` | 128, 128b, 129, 130 | first index where a running sum crosses a threshold; basement 0/30 -> 30/30; DEFAULT 0.3 since 130; joint battery with --term-expr: 755/770 both arms (129r) |
| `--pair-find P`, `--parity-term P` | 130, 131 | pair-that-hits-a-target block; positional (parity) digit-fold sum; DEFAULT 0.05 / 0.03 since 131 (high P cost elements_twice 29 -> 24); PSB2 find_pair 30/30, luhn 28/30 |
| `--sum-if P`, `--radix P` | 131 | conditional sum; mixed-radix decomposition (divisor cap 30 tuned to coins); DEFAULT 0.05 / 0.03; PSB2 paired_digits 30/30, coin_sums 2/30 |
| unseen PSB2 test | 132 | six pieces ON vs OFF on cut_vector/fizz_buzz/mastermind: identical (cut_vector degenerate, others 0/30); no evidence of generalization beyond the designed-for problems |
| unseen PSB2 test 2 | 133 | twitter, middle_character, solve_boolean, leaders: ON 2/20 vs OFF 0/20 on middle_character only (noise); still no evidence of generalization |
| unseen PSB1-style test | 134 | ten own-generator problems: ON = OFF = 40/200 (mirror_image, grade at ceiling; eight at floor); 17 unseen problems in 132-134, no ON/OFF difference beyond noise |
| medium PSB1-style test | 135 | OFF 65/200 vs ON 62/200; last_index_of_zero 15 vs 14, negative_to_zero 10 vs 8, six problems still 0/20; no gain beyond noise outside the designed-for seven |
| count-term / square-term (opt-in) | 136, 137 | close the count_odds / sum_of_squares gaps (reach 1/10, 10/10) but battery drops majority_value 5 -> 3 and count_multiples_of_three stays 0/20; not promoted |
| `--count-mod P` | 138 | `arr[i] % c ==/!= 0` CountIf (c in 2..9); DEFAULT 0.1; modular-count family 0/80 -> 63/80, control 0/20; battery 758 vs 755 |
| PSB2 integer subset, 100 seeds | 139-145 | 595/700 at 900 s (fuel_cost 91, luhn 90, coin_sums 14); 1800 s: fuel_cost 94, luhn 98 (luhn failures were budget; six fuel_cost failures exhaust the size limit, sec. 142); parity-term 0 hurts fuel_cost (81); six pieces off at 100 seeds: 113/700 (sec. 142); random search (no beam feedback) with pieces on matches the beam, 187 vs 183 of 210 at 30 seeds (sec. 143); frozen held-out (median, square_digits, bowling, camel_case; 30 seeds): pieces do not transfer, median 30/30 with and without them, random 0/30, the other three 0 in every arm (sec. 145); PushGP rerun not feasible here (Clojush psb2-v1.0: 190-277 s per generation on fuel_cost, 16-22 h per non-solving run, sec. 146) |
| Learning curve on fresh problems | 149 | 30-problem pool, random draw into 3 batches of 10, library frozen per batch, predictions registered before implementation; batch 1 (sec. 149r): 3 of 10 problems solved by the beam (cumulative_max, is_power_of_two, swap_adjacent_pairs, in 8, 3, 2 of 30), random 0, pieces off 3; scalar loops (digit_sum, popcount, alternating_sum, sum_multiples) 0 of 30; batch 2 (sec. 150r, after three new pieces, sec. 150): beam 6 of 10 (M1 3 -> 6 = rise), random 2, forecast of a flat curve failed; rise is mostly reuse of earlier pieces; batch 3 (sec. 151r): beam 3 of 10 (M1 3, 6, 3: no trend), random 0; prefix_sums and digital_root 0 of 30 against predictions; 3 of 7 predictions held |
| leave-one-out of six pieces (30 final seeds) | 147 | term-expr, prefix-cross, pair-find, parity-term, sum-if each indispensable for its problem (<=2/30 without); radix redundant for paired_digits (30/30 without); no interaction on bystanders |
| `--forbid-stmt`, `--fill-bsearch-hole`, `--ablate-skeleton`, `--fill-tsp-guard` with `--soundness-labels --metamorphic --rank-worst-scale` | 63-69, 43-44, 118 | research modes; the TSP guard pipeline (43-44) is a positive result and the only code that reproduces it |

## 4. Closed as negative (do not retry without a new idea)

- Per-case (lexicase) selection, `--lexicase-rate` (153, 153r, 153c): no problem at zero is reached (digital_root plateau at 177 of 200 unchanged); at most +8 solved runs of 180 on six reachable problems (p = 0.33 on new seeds). Opt-in, not promoted.
- `--max-const 10` (152r): digital_root 1 of 20 against 0; the literal range 0..3 is not the blocker.
- Mined statement library (154) and a loop skeleton with an enumerated update expression (155, 156): the key statements of the unsolved problems are absent from solved programs; the scalar-recurrence loop is 9-13% of fresh candidates but the array-update loop 0.014%, and the enumerated piece would reach at most 2 of 8 problems and needs staged evaluation. Not built.
- Sorting diversity funnel (157): without sort skeletons fresh sorters are one nested compare-swap template (0.08% with 8 variables); wrapping the adjacent pass in an outer repeat makes 8% of those programs bubble sort, so the gap is a missing composition move, not budget; no lever built.
- Outer-repeat move (158, opt-in `--outer-repeat`): fresh bubble sorters 0 -> 24 per 600 000 candidates, but only 1 of 8 searches keeps one (predicted >= 4); the cheaper exchange template takes the beam at equal score. Necessary, not sufficient; levers 2 (insertion) and 3 (recursion) pending.
- Nested adjacent pass (159, opt-in `--adjacent-nested`): fresh insertion sorters 0 -> 21 per 600 000 candidates and insertion sort kept in 7 of 8 searches (predicted <= 2); novelty alone adds nothing. With 158 and 159 the search holds exchange, insertion and (1 of 8) bubble without a sort skeleton. Lever 3 (recursion: merge, quick) pending.
- Recursion funnel (160): sibling split calls 1e-4 of fresh candidates, partition leaf <= 1.7e-5, merge take step 0; joint rate below 1e-9, so the missing parts are leaves, not a wrapper. Candidate lever: a generic partition block first (not built).
- Partition block (161, opt-in `--partition`): the leaf is reached but no recursive sorter appears (0 of 1.2M fresh, 0 of 8 searches); the calls never run on the sorting path. Lever 3b (range-recursion shell around the leaf) next.
- Range-recursion shell (162, opt-in `--range-shell`): quick sort appears in fresh generation (172 per 1.2M candidates); by default only 2 of 8 searches keep a correct one (corrected count, 163b), because the 39-43 node exchange template closes the levels first. Merge sort not built.
- Size-order control (163, opt-in `--start-size`): with sizes below 50 skipped and the shell dominant, 7 of 8 searches keep a real Lomuto quick sort (corrected count: `triage_sorting` files it under ABORT_COST:depth because of the 200-deep recursion cap on sorted n=256; `scripts/recursive_kept.py` counts it). The size-order hypothesis of 162 is supported. Seeding the beam with 172 recursive sorters (163b) does not beat the control.
- Recursion cap (164, opt-in `--max-recursion-depth`, triage `TRIAGE_MAX_DEPTH`): with cap 1000 the 163 quick sorts triage as OK with core exponent 1.12-1.13 (7 of 8 seeds, 76 programs vs 46 at cap 200; single runs, no noise estimate, so the count gain is suggestive).
- Joint battery (165, BASE vs ALL four opt-in moves, two rounds of 8 seeds): BASE keeps one family per seed (exchange); ALL keeps exchange + insertion in 16 of 16 seeds and quick sort in 8 of 16 (4 and 4), 2.75 families per seed against 1.00; all six predictions hold, rounds agree within 1 seed. Promotion candidate; leave-one-out needed first, nothing promoted. Bubble stays rare (1 genuine of 16).
- Leave-one-out (166, round 1 of 8 seeds per arm): `--outer-repeat` droppable (no change without it); `--adjacent-nested` needed for insertion (8 -> 0) and `--range-shell` needed for quick sort (4 -> 0); `--partition` ambiguous in both rounds (quick 4 -> 2 each time, exchange and insertion -1 to -2); kept, direction consistently positive. Reduced candidate = ALL minus outer-repeat; not promoted, no cross-problem battery yet. Without the adjacent pass quick sort is kept in 8 of 8 seeds: insertion and the shell compete for the same levels.
- Cross-problem battery of the sorting moves (167, adjacent-nested 0.3 + partition 0.1 + range-shell 0.05 vs default, 77 problems x 10 seeds): 757 vs 759 solved, 75 problems equal, elements_twice 10 -> 9 and majority_value 3 -> 2, nothing gains; no drop of 2 but the aggregate is lower, so by the pre-registered rule the moves stay opt-in. Isolation of the two problems at 30 seeds not run.

- Population diversity, niching, crossover, annealing, score bands as ways to
  find new algorithms (5, 20, 21, 28; later removed). The grammar prior is what
  matters (5).
- Shape-only skeletons, enumerative repair of scrambled skeletons, joint pairs
  and SMT sketching (66-68): shape alone does not compose; 100 of 112
  skeleton sites are rebuilt by enumeration but not the shape (65).
- Adaptive family weights and a protected skeleton quota (59, 60).
- Nested placement to replace `selectionsort` (102, 102b), canonical outer
  tests (73, 74).
- Role-typed variables (37), generation-time pruning (33-35).
- Two-call divide-and-conquer shell (119): never produces a recursive
  reduction.
- Third interval-search shape, minimum of a rotated array (123): 0 hits.
- Sorting without swap and without the merge skeleton (118b, 118c): 0
  generalizing programs. Selection and demotion levers are exhausted; the
  limit is reachability in the grammar (118).

## 5. Results worth knowing

- Sorting reaches heap sort in a minute and O(n log n) (14, 92); bubble and
  gnome sort appear by composition when the sort skeletons are off (46);
  there are two or three algorithm families only (92, 93).
- `max_subarray_sum`: only Kadane (96). `three_sum`: only the skeleton (49).
- Open-specification problems (`any_peak`, `any_valley`, `any_duplicate`,
  property checker instead of one reference answer, 120): `any_peak` 10/10 with
  six behavior classes, all linear scans.
- One-call interval shell (121-124): binary-search peak in 9 of 10 seeds, key
  membership search in 5 of 10 verified (121c). This is assembly of a
  known algorithm from a small piece that already carries the shape (key,
  clamp, flag), not a discovery of binary search. Not promoted (124).
- Non-canonical finds so far are small: gnome sort with an uninitialized
  variable, Fung's sort, a point mutation for `has_adjacent_equal`.
- Open question the log keeps returning to: how to get an algorithm no one
  wrote down. Candidates not yet tried: cost-optimal specifications (fewest
  comparisons) and neutral drift on large programs.

## 6. Lessons that change how experiments are run

- Register predictions and the decision rule before running; report negatives.
- Matched seeds. Joint battery before promoting; time is not a gate (promote on
  reachability and diversity).
- `--start-level 2` when level 1 is trivial (`arr[0] = 0`), otherwise every
  record is UNCONFIRMED (119 round 1, 120). The converse hole: a run that
  starts at level 2 never trains on a one-element input, so adversarial sets
  must cover every size (121c, 122).
- `--profile-ban` and `--post-solve-sizes` depress base counts (rotated_min
  3/10 with them, 10/10 without, 124): compare like with like.
- Python triage must seed `v1/v2 = 0/n` when `--recursion-call-bias > 0`
  (`scripts/triage_open_spec.py --seed-rec`); otherwise every recursive program
  looks wrong.
- Verify a "hit" outside the engine (400 inputs, sizes 1-256); the engine's
  adversarial set missed 3 of 8 wrong variants once (121c).
- Long runs: `setsid nohup`, never `pkill -f` on the command's own text.
- Reproducing an old section: the cleanup of sections 125-125c removed 27
  flags and moved 40 scripts to `scripts/archive/`; `git checkout pre-cleanup`
  (or `pre-cleanup2`, `pre-cleanup3`) runs them unchanged.

## 7. Where each topic lives

| topic | sections |
|---|---|
| origins: solved needs care, overfitting, grammar prior | 1-9 |
| curriculum problems, complexity ceilings, TSP, DTW | 10-19 |
| binary search ladder, skeleton catalogue, divergence engine | 20-30 |
| compositional gap, scoped conjunction, bloat, redundancy | 31-37 |
| combinator algebra and its promotion | 38-48, 52-56 |
| TSP guard by enumeration, soundness, metamorphic scaling | 41, 43, 44 |
| sorting diversity | 45, 46, 92-98, 118 |
| skeleton replacement by generic pieces | 56-58, 99-109 |
| shape vs fills (enumeration, SMT) | 63-69 |
| the stepwise promoted pieces | 70-91, 100-115 |
| external held-out sets and diagnosis | 114, 116 |
| restarts, confirm-reseed, cost-profile novelty | 94-98 |
| open specification and interval shells | 119-124 |
| comparison with published work (`docs/literature.md`), PSB2 (results: 4/180 held-out solved) | 126 |
| cleanup | 125-125c |

The per-section list with chunk and line is `docs/findings/INDEX.md`.
- 170-172: selection block and merge shell moves, calibration battery, large families run reaches 5 families (exchange, insertion, quick, selection, merge); moves stay opt-in, one arm per family
- 173-178: sort block transfers poorly to neighbours (173-174: 0 solved, the adjacent pass is the hard half); the pass alone is out of reach of generic search (175: 0/50 without pieces, 50/50 with them); opt-in `--prefix-combinator` (free prefix + one combinator block) composes sort and pass without sorting skeletons (176-177: min_gap 0 -> 10, max_gap_sorted 1 -> 10) but costs 4 of 759 on the suite (178b), stays opt-in; merge/quick need the sorter given (178a: free prefix with large `--start-size` stalls at level 3; 178c: `--prefix-programs` library of discovered sorters + guarded block, 10/10 for selection, quick and merge)
- 179: the unguarded combinator block after a recursive prefix was a bug (178a); with the `if ((v2 - v1) >= v0)` guard the free prefix (`--prefix-combinator 0.3` + `--merge-shell`/`--partition`) finds merge+pass 20/20 and quick+pass 16/20 on min_gap/max_gap_sorted, default start size; still opt-in
- 180: full sorting profile (prefix 0.3 + selection + adjacent-nested + partition + range-shell) costs 7 of 759 on the 70-problem suite (elements_twice -3, count_dominated -2, least_frequent_value -2); rule not met, stays opt-in as a sorting profile
- 181: a single sorting profile (prefix 0.3 + selection 0.1 + partition 0.1 + range-shell 0.05 + merge-shell 0.1) solves min_gap and max_gap_sorted 60/60 in minutes but covers only selection (46) and merge (14); quick never, so per-family arms (or retuned partition weights) stay necessary
- 182: with `--partition 0.3 --range-shell 0.5` the single sorting profile reaches three families through min_gap/max_gap_sorted (selection 38, merge 14, quick 9 of 60; 60/60 solved); opt-in sorting profile, its suite cost at these weights is not measured
- 183: the retuned sorting profile (182) costs 14 of 759 on the 70-problem suite (elements_twice -3, least_frequent_value -3, count_dominated/count_leaders/max_drawdown -2); sort-based problems lose nothing; not promoted, documented as an opt-in sorting switch
- 185: README results refreshed from the 759/770 base arm; fitted complexity misleads for gcd (O(1) fit, Euclid is O(log n): benchmark instance gcd(n, 1)), factorial, three_sum, graph_reachability; is_prime now fits O(n) in 8 of 10 seeds
- 186: C superoptimizer prototype phases 0-1 (`docs/superopt.md`): Add/Sub overflow UB removed (byte-identical), opt-in wraparound Mul; `algosearch c-import` C subset -> Mini-C -> C, round trip 15/15 against gcc -O2 -fwrapv, negative control caught
- 187: `--problem c:FILE.c` (ReferenceProblem, the C function as oracle, wraparound only for it, byte-identical elsewhere): unseeded search rediscovers 13/45 (array_sum, reverse, insertion→selection sort, max, abs 1/3), all 13 compiled-equivalent; scalar bit functions stall at level 1 within the 150-size-step budget (runs end in 5-34 s); next is phase 3 (seed with the reference, cost)
- 188-188b: `algosearch superopt` (phase 3: from the reference, cost-model beam + exploration beam, CEGIS): first run exposed an n = 0 hole (a wrong result, fixed), verification and emitter-cost defects and timing below resolution (fixed: `c_speedup.py --inline`); rerun 45/45 compiled-equivalent, one real gain (is_pow2_loop 1.15-1.34x, two zero bits per iteration; self-control 1.00); model vs measured Spearman 0.12, ctz 0.55-0.60x: the cost must come from compiled timing next
- 189: `superopt --time-top 4` (compiled timing of candidates inside the loop, original and candidate in one TU/process, noise threshold from a self-control, reference returned unless measurably faster): 45/45 equivalent, no regression below 0.963, is_pow2 1.17-1.20x and a new gain the model cannot see, sign 1.64x (`(x > -1) - (x <= 0)`, a gcc codegen effect); recommended superopt mode
- 190/190b: phase 3c algorithmic moves (`superopt`, now default; `--no-algorithmic-moves` = phase 3b). With the in-loop timer on 24 repeated inputs, two branchy unrolls timed 1.24x/1.42x but ran 0.34x/0.92x (branch predictor memorized the workload); fixed with 512 fresh timing inputs. Rerun: 138/138 equivalent, gains on 7 samples vs 4 (ctz 1.29x, Kernighan popcount 1.97x, count_digits 1.55x, all branch-light unrolls), no search-made candidate below 0.95 (one returned reference at 0.946, sub-ns noise; strict reading of the rule would keep it opt-in)
- 191: phase 4a, C front end for everyday code (constant `#define`, `INT_MAX`/`INT_MIN`, inlined `return expr;` helpers, `continue` in for/do-while, auto target); 8 real-style samples round-trip, superopt gains on 1 of 6 measurable (sum_abs_diff 1.14x, unroll + branch-free abs). Found that timed superopt wrote the re-emitted reference even when it compiled slower (sat_add 0.28x: `&&` lowered to `&`); now it returns the original text when nothing is measurably faster. Gaps for real code: loop-invariant hoisting moves, user-given workloads
- 192/192b: phase 4b, real code. Census: 12/2060 musl and 1/1125 Lua functions import alone (gates: pointer/library types, string/char literals, integer suffixes, long/unsigned/char). On musl's is_leap the first run returned WRONG code on all 3 seeds (dropped the `y > INT_MAX-1900` overflow guard; no test reached that tail); fixed with boundary inputs from the reference's folded constants, a compiled check on all 2^32 arguments for unary total functions (`equivalence_level exhaustive`), and domain-aware verification (`Problem::definesAnswer`). Rerun: is_leap 2.7-4.0x proved (the `(y+1900) & 15` trick); 25 earlier unary gains re-proved; days_in_month 3x is branch-block order, not algorithmic
- 193: character literals (ASCII, escapes; >127 rejected for char signedness). Census +1 musl (isblank) +1 Lua (digit): the functions using `'` almost all also need `char`/`unsigned`/pointers. isblank 1.20x `exhaustive`, but codegen (`||` as `+` of disjoint compares), not algorithmic; digit returns the reference
- 194: 32-bit `unsigned` (typed lowering: unsigned compares as sign-flipped, logical `>>` masked; `u` and big hex literals; unsigned `/` `%` rejected). musl 13 → 32 import alone. First run: a stale binary voided 7 samples and checkint returned a WRONG `tested` winner (exponent bounds ±1) -> exhaustive gate now for every unary function, mismatches counted only where the reference is defined (refuted 6 checkint winners in the rerun). 7/18 gain on uniform ints, but character functions need character workloads (`c_speedup.py --inputs-file`): hexval/isspace/ascii_to_bin gains vanish; isalpha (`setl` for 2-µop `setbe`, Intel), iscntrl, zeroinfnan and setup_salt (loop unrolled, counter eliminated, proved on 2^32) hold
- 195: `superopt --workload FILE` (the user's inputs drive the compiled timing and model cost, join the tests; correctness still on all inputs; battery `EVAL_INPUTS` for held-out external timing). On 10 character samples, held-out text/hex/b64: no candidate regression (random-workload outputs: hexval 0.23x, isspace 0.78x); isspace gains 2.3-2.5x with a text-tuned early exit; 4 vs 3 gaining samples. Results are workload-specific by design
- 196: unsigned `/` and `%` lowered to signed division (Hacker's Delight 9-3), tested against native C; census unchanged (musl 32, Lua 2): the one function it gated (casemap) stops next at a global table. Next real-code gates: pointer/char/long/size_t types, string literals, L suffixes
- 197/197b: `char`/`short` scalars (truncate on storage, promote in expressions; scripts call through typed wrappers). Census musl 32 -> 33 only: the real gate behind `char` is `char *`. A superopt winner was fast only on the input sequence the search timed (1.53x there, 0.57x held-out): the top three are now confirmed on inputs the loop never timed (alternate half of --workload, or a second random set). Rerun: lucky winner gone, the four real character gains stay (1.11-3.71x held-out)
- 198: ceiling study, best hand version vs search on the 27 accepted samples, on 65,536 non-repeating inputs. 512 repeated inputs (in-loop timer and external checks since 190b) let the predictor learn branches: a proved zeroinfnan winner is 0.18x (5x slower) and filterpc's branch-free 5.9x gain is invisible. Gaps hand/search >= 1.10 on 5/27, 3 expressible in Mini-C (filterpc 5.9, setup_salt 7.0, is_leap 1.3; not hexval table, swapc bswap); hexval table 21.7x on uniform hex. The 197b isspace winners (3.7x/2.8x on 1024 repeated bytes) run at 0.39x/0.41x on 65,536 bytes of text. First bottleneck: the search and its timer, not coverage
- 198b: in-loop timer on 65,536 non-repeating inputs (commit 90cc143), 27 samples x seeds 10-12, old winners re-measured in the same session: 0/81 results below 0.95 (old arm: zeroinfnan 0.11x twice, isspace 0.37/0.41x); filterpc 13.6x (beats the hand version), ascii_is_unsafe 3.6x on b64; 190-rule gains on 11 samples vs 4. Remaining gaps: setup_salt 6.9, is_leap 1.85 (Mini-C), hexval table, swapc bswap. Speedup ratios moved up to 2.7x between sessions with self-controls at 1.00: compare arms within one session only
