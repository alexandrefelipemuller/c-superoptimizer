#!/usr/bin/env bash
# Section 168: isolation of the two problems that lost a run in section 167 (elements_twice, majority_value),
# base against PIECE (--adjacent-nested 0.3 --partition 0.1 --range-shell 0.05), 30 matched seeds, 180 s.
# Usage: scripts/run_sorting_moves_isolation.sh output_root
# Env: SEEDS (30), SEED_BASE (60100), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-30}"; SEED_BASE="${SEED_BASE:-60100}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for problem in elements_twice majority_value; do for arm in base PIECE; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm $problem $((SEED_BASE + s))"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  set --
  if [ "$arm" = PIECE ]; then
    set -- "$@" --adjacent-nested 0.3 --partition 0.1 --range-shell 0.05
  fi
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "SORTING_MOVES_ISOLATION_DONE $(date +%T)" >> "$D/driver.log"
