#!/usr/bin/env bash
# Section 171: calibration battery for the large sorting-families run. Four arms that differ in --start-size and move
# mix, sorting profile of section 93 (skeletons selectionsort/heapsort/mergesort/binarysearch off).
# Usage: scripts/run_sorting_families_battery.sh output_root [wall_seconds_per_seed] [seeds...]
# Arms: A0 start 0 (all moves moderate), A30 start 30 (selection+insertion), A55 start 55 (quick), A80 start 80 (merge).
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?output_root}"; WALL="${2:-300}"; shift 2 2>/dev/null
SEEDS=("$@"); [ $# -eq 0 ] && SEEDS=(1 2 3 4)
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
ALLM="--adjacent-nested 0.3 --partition 0.1 --range-shell 0.05 --selection-block 0.1 --merge-shell 0.05"
declare -A ARMS=(
  [A0]="$ALLM"
  [A30]="--adjacent-nested 0.3 --selection-block 0.3 --partition 0.1 --start-size 30"
  [A55]="--adjacent-nested 0.1 --range-shell 0.5 --partition 0.1 --start-size 55"
  [A80]="--merge-shell 0.5 --range-shell 0.05 --start-size 80"
)
for arm in A0 A30 A55 A80; do
  EXTRA="${ARMS[$arm]}" scripts/run_sorting_seeds.sh "$OUT/$arm" "$WALL" "${SEEDS[@]}" >> "$OUT.driver.log" 2>&1
done
echo "SORTING_FAMILIES_BATTERY_DONE $(date +%T)" >> "$OUT.driver.log"
