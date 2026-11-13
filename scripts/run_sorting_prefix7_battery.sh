#!/usr/bin/env bash
# Section 182: retune partition / range-shell of the section 181 profile. Arms P2 (--partition 0.2 --range-shell 0.2) and
# P3 (--partition 0.3 --range-shell 0.5); the rest as 181 (--prefix-combinator 0.3 --selection-block 0.1 --merge-shell 0.1,
# recursion bias 0.1, skeletons off, nested/distinct/state/frequency pieces off). Same 30 seeds per problem (70000-70029).
# Usage: scripts/run_sorting_prefix7_battery.sh output_root
# Env: WALL (600), SEEDS (30), SEED_BASE (70000), JOBS (8).
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
WALL="${WALL:-600}"; export WALL
SEEDS="${SEEDS:-30}"; SEED_BASE="${SEED_BASE:-70000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for arm in P2 P3; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    P2) set -- --partition 0.2 --range-shell 0.2 ;;
    P3) set -- --partition 0.3 --range-shell 0.5 ;;
  esac
  timeout $WALL ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct --no-combinator-state \
    --no-combinator-frequency --prefix-combinator 0.3 --selection-block 0.1 --merge-shell 0.1 "$@" \
    --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX7_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
