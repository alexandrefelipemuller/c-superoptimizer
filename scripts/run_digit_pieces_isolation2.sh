#!/usr/bin/env bash
# Section 150c: second isolation after the first (section 150b rule): digit-loop and signed-parity each alone at half probability (0.015), the same two
# problems and 30 seeds as run_digit_pieces_isolation.sh (the base arm is reused from that run).
# Usage: scripts/run_digit_pieces_isolation.sh output_root   Env: JOBS (8)
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for s in $(seq 60100 60129); do for p in least_frequent_value majority_value; do for arm in digit015 signed015; do
  echo "$arm $p $s"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    digit015) f="--digit-loop 0.015 --counter-mod 0 --signed-parity 0" ;;
    signed015) f="--digit-loop 0 --counter-mod 0 --signed-parity 0.015" ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 $f --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "ISOLATION2_DONE $(date +%T)" >> "$D/driver.log"
