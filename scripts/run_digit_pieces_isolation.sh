#!/usr/bin/env bash
# Section 150: the joint battery showed drops of 2 on least_frequent_value (10 -> 8) and majority_value (4 -> 2). Isolation: base (three
# pieces at 0), each piece alone at 0.03, and all three, on those two problems, 30 matched seeds (60100-60129), 180 s.
# Usage: scripts/run_digit_pieces_isolation.sh output_root   Env: JOBS (8)
set -uo pipefail
cd "$(dirname "$0")/.."
D="${1:?output_root}"; JOBS="${JOBS:-8}"
mkdir -p "$D"
for s in $(seq 60100 60129); do for p in least_frequent_value majority_value; do for arm in base digit counter signed all; do
  echo "$arm $p $s"
done; done; done | xargs -P "$JOBS" -L1 sh -c '
  arm=$0; problem=$1; seed=$2
  case $arm in
    base) f="--digit-loop 0 --counter-mod 0 --signed-parity 0" ;;
    digit) f="--digit-loop 0.03 --counter-mod 0 --signed-parity 0" ;;
    counter) f="--digit-loop 0 --counter-mod 0.03 --signed-parity 0" ;;
    signed) f="--digit-loop 0 --counter-mod 0 --signed-parity 0.03" ;;
    all) f="--digit-loop 0.03 --counter-mod 0.03 --signed-parity 0.03" ;;
  esac
  timeout 180 ./algosearch --problem $problem --max-n 8 --seed $seed --threads 1 --beam-width 500 \
    --candidates 1500 --max-program-size 150 --step-limit 20000 $f --benchmark --benchmark-seeds 1 \
    > '"$D"'/log_${arm}_${problem}_${seed}.txt 2>&1
  echo "$arm $problem $seed exit=$? $(date +%T)" >> '"$D"'/driver.log'
echo "ISOLATION_DONE $(date +%T)" >> "$D/driver.log"
