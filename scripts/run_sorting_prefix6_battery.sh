#!/usr/bin/env bash
# Section 181: family coverage of ONE sorting profile through the composed problems min_gap / max_gap_sorted.
# PROF = --prefix-combinator 0.3 --selection-block 0.1 --partition 0.1 --range-shell 0.05 --merge-shell 0.1 (no --adjacent-nested: it
# rewrites the pass), default start size, skeletons off, nested/distinct/state/frequency pieces off, recursion bias 0.1.
# 30 seeds per problem, WALL (600 s), N = 8. Classify the printed solutions with scripts/classify_logs.py.
# Usage: scripts/run_sorting_prefix6_battery.sh output_root
# Env: WALL (600), SEEDS (30), SEED_BASE (70000), JOBS (8).
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
WALL="${WALL:-600}"; export WALL
SEEDS="${SEEDS:-30}"; SEED_BASE="${SEED_BASE:-70000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "PROF $problem $((SEED_BASE + s))"
done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  timeout $WALL ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct --no-combinator-state \
    --no-combinator-frequency --prefix-combinator 0.3 --selection-block 0.1 --partition 0.1 --range-shell 0.05 \
    --merge-shell 0.1 --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX6_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
