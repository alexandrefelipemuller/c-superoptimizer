#!/usr/bin/env bash
# Section 173: transfer of the sorting moves to neighbouring problems. Problem-specific pieces are off (--combinator-bias 0)
# so the base does not already solve them. Arms: CTRL (sorting profile flags only), SEL (--selection-block 0.1),
# MRG (--merge-shell 0.1), PQ (--partition 0.1 --range-shell 0.05). 10 matched seeds, 180 s, N = 8.
# Usage: scripts/run_sorting_transfer_battery.sh output_root
# Env: SEEDS (10), SEED_BASE (61000), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-61000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in min_gap max_gap_sorted count_distinct inversion_count lis; do for arm in CTRL SEL MRG PQ; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    SEL) set -- --selection-block 0.1 ;;
    MRG) set -- --merge-shell 0.1 ;;
    PQ)  set -- --partition 0.1 --range-shell 0.05 ;;
    *)   set -- ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 --combinator-bias 0 "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "SORTING_TRANSFER_DONE $(date +%T)" >> "$D/driver.log"
