#!/usr/bin/env bash
# Section 178: prefix + combinator block with the merge and quick families. Sorting skeletons disabled; nested/distinct/
# state/frequency pieces off in every arm (as section 177). Arms: C80 (--start-size 80, no prefix), MRG (C80 +
# --prefix-combinator 0.3 --merge-shell 0.8), C55 (--start-size 55), QS (C55 + --prefix-combinator 0.3 --partition 0.3
# --range-shell 0.5). Problems min_gap, max_gap_sorted; 10 matched seeds, 180 s, N = 8.
# Usage: scripts/run_sorting_prefix3_battery.sh output_root
# Env: WALL (180 s), SEEDS (10), SEED_BASE (67000), JOBS (8).
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
WALL="${WALL:-180}"; export WALL
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-67000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for arm in C80 MRG C55 QS; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    C80) set -- --start-size 80 ;;
    MRG) set -- --start-size 80 --prefix-combinator 0.3 --merge-shell 0.8 ;;
    C55) set -- --start-size 55 ;;
    QS)  set -- --start-size 55 --prefix-combinator 0.3 --partition 0.3 --range-shell 0.5 ;;
  esac
  timeout $WALL ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct --no-combinator-state \
    --no-combinator-frequency "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX3_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
