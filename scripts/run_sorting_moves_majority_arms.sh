#!/usr/bin/env bash
# Section 169: one arm per sorting move on majority_value, 30 matched seeds (60100-60129), 180 s.
# Arms: AN (--adjacent-nested 0.3), PA (--partition 0.1), RS (--range-shell 0.05). Base and PIECE are in section 168.
# Usage: scripts/run_sorting_moves_majority_arms.sh output_root
# Env: SEEDS (30), SEED_BASE (60100), JOBS (8).
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"
SEEDS="${SEEDS:-30}"; SEED_BASE="${SEED_BASE:-60100}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for arm in AN PA RS; do for s in $(seq 0 $((SEEDS - 1))); do
  echo "$arm majority_value $((SEED_BASE + s))"
done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    AN) set -- --adjacent-nested 0.3 ;;
    PA) set -- --partition 0.1 ;;
    RS) set -- --range-shell 0.05 ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 "$@" --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "SORTING_MOVES_ARMS_DONE $(date +%T)" >> "$D/driver.log"
