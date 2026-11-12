#!/usr/bin/env bash
# Section 178c: sorter library as fixed prefix + free combinator block. Sorters discovered in the big run (section 172),
# stored in scripts/seeds/prefix_{selection,quick,merge}.txt. Arms: CTRL (no library), LS/LQ/LM (--prefix-programs FILE
# --prefix-library-count 300). Sorting skeletons disabled; nested/distinct/state/frequency pieces off in all arms;
# default start size. Problems min_gap, max_gap_sorted; 10 matched seeds, WALL (300 s), N = 8.
# Usage: scripts/run_sorting_prefix4_battery.sh output_root
# Env: WALL (300), SEEDS (10), SEED_BASE (68000), JOBS (8).
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
WALL="${WALL:-300}"; export WALL
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-68000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted; do for arm in CTRL LS LQ LM; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    LS) set -- --prefix-programs scripts/seeds/prefix_selection.txt --prefix-library-count 300 ;;
    LQ) set -- --prefix-programs scripts/seeds/prefix_quick.txt --prefix-library-count 300 ;;
    LM) set -- --prefix-programs scripts/seeds/prefix_merge.txt --prefix-library-count 300 ;;
    *)  set -- ;;
  esac
  timeout $WALL ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct --no-combinator-state \
    --no-combinator-frequency "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX4_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
