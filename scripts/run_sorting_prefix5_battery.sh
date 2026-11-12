#!/usr/bin/env bash
# Section 179: free recursive prefix (quick / merge shells) + guarded combinator block, default start size. Sorting
# skeletons disabled; nested/distinct/state/frequency pieces off in all arms. Arms: CTRL (no prefix), QS2 (--prefix-combinator 0.3
# --partition 0.3 --range-shell 0.5), MRG2 (--prefix-combinator 0.3 --merge-shell 0.8). min_gap, max_gap_sorted; 10 matched seeds.
# Usage: scripts/run_sorting_prefix5_battery.sh output_root
# Env: WALL (600), SEEDS (10), SEED_BASE (69000), JOBS (8).
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
WALL="${WALL:-600}"; export WALL
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-69000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for arm in CTRL QS2 MRG2; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    QS2)  set -- --prefix-combinator 0.3 --partition 0.3 --range-shell 0.5 ;;
    MRG2) set -- --prefix-combinator 0.3 --merge-shell 0.8 ;;
    *)    set -- ;;
  esac
  timeout $WALL ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct --no-combinator-state \
    --no-combinator-frequency "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX5_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
