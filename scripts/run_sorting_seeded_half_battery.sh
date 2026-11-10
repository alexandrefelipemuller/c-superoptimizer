#!/usr/bin/env bash
# Section 174: fair transfer test, the seeded half. From level 2, the search is seeded with a program that already solves
# levels 1-2 of the gap problems (n <= 2: the gap of the two values) and must generalise the pass to larger n.
# Arms: CTRL2 (unseeded, start level 2), S0 (seed without sort: |a - b|), S1 (seed with selection sort then arr[1] - arr[0]).
# Usage: scripts/run_sorting_seeded_half_battery.sh output_root
# Env: SEEDS (10), SEED_BASE (63000), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-63000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for arm in CTRL2 S0 S1; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    S0) set -- --seed-programs scripts/seeds/gap_nosort_first_two.txt ;;
    S1) set -- --seed-programs scripts/seeds/gap_sort_first_two.txt ;;
    *)  set -- ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --start-level 2 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --combinator-bias 0 "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "SEEDED_HALF_DONE $(date +%T)" >> "$D/driver.log"
