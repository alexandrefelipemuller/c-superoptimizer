#!/usr/bin/env bash
# Section 175: is the adjacent pass alone findable? Problems that are one adjacent pass (no sorting), generic search
# (GEN: --combinator-bias 0) against the default combinator pieces (PIECE). Same other flags as sections 173-174.
# Usage: scripts/run_sorting_pass_battery.sh output_root
# Env: SEEDS (10), SEED_BASE (64000), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-10}"; SEED_BASE="${SEED_BASE:-64000}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in max_adjacent_gap max_rise min_step total_variation count_descents; do for arm in GEN PIECE; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    GEN) set -- --combinator-bias 0 ;;
    *)   set -- ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 --num-vars 8 --recursion-call-bias 0.1 \
    --second-array-bias 0.05 "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "PASS_BATTERY_DONE $(date +%T)" >> "$D/driver.log"
