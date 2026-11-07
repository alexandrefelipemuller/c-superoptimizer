#!/usr/bin/env bash
# Section 172: large sorting-families run. Arms from the calibration of section 171, sequential (each run uses every thread).
# Usage: scripts/run_sorting_families_big.sh output_root [wall_seconds_per_seed]
# Env: N_A0 (8), N_A30 (4), N_A55 (10), N_M80 (16): seeds per arm; seeds are 1000+i so they do not overlap section 171.
# Arms: A0 start 0, all moves moderate; A30 start 30 (selection, insertion); A55 start 55 (quick); M80 start 80 (merge only).
# Afterwards: scripts/sorting_families_report.sh output_root
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?output_root}"; WALL="${2:-1200}"
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
declare -A N=([A0]="${N_A0:-8}" [A30]="${N_A30:-4}" [A55]="${N_A55:-10}" [M80]="${N_M80:-16}")
declare -A ARMS=(
  [A0]="--adjacent-nested 0.3 --partition 0.1 --range-shell 0.05 --selection-block 0.1 --merge-shell 0.05"
  [A30]="--adjacent-nested 0.3 --selection-block 0.3 --partition 0.1 --start-size 30"
  [A55]="--adjacent-nested 0.1 --range-shell 0.5 --partition 0.1 --start-size 55"
  [M80]="--merge-shell 0.8 --start-size 80"
)
mkdir -p "$OUT"
for arm in M80 A55 A0 A30; do
  seeds=(); for i in $(seq 1 "${N[$arm]}"); do seeds+=($((1000 + i))); done
  EXTRA="${ARMS[$arm]}" scripts/run_sorting_seeds.sh "$OUT/$arm" "$WALL" "${seeds[@]}" >> "$OUT/driver.log" 2>&1
done
echo "SORTING_FAMILIES_BIG_DONE $(date +%T)" >> "$OUT/driver.log"
