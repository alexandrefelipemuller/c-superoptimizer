#!/usr/bin/env bash
# Section 176: free prefix + combinator block. Arms: CTRL (default pieces with the nested and distinct pieces off, so
# min_gap/max_gap_sorted/count_distinct cannot be solved by a single pair piece), PRE (CTRL + --prefix-combinator 0.3
# with sort moves for the prefix: --selection-block 0.3 --partition 0.1 --range-shell 0.05). 10 matched seeds, 180 s, N = 8.
# Usage: scripts/run_sorting_prefix_battery.sh output_root
# Env: SEEDS (10), SEED_BASE (65000), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-65000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted count_distinct; do for arm in CTRL PRE; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    PRE) set -- --prefix-combinator 0.3 --selection-block 0.3 --partition 0.1 --range-shell 0.05 ;;
    *)   set -- ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --no-combinator-nested --no-combinator-distinct "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PREFIX_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
