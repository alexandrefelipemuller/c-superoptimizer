#!/usr/bin/env bash
# Section 172: resume after the low-clock pause. Runs the seeds still missing at full clock.
# Usage: scripts/resume_sorting_families_big.sh output_root
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?output_root}"
export ALGOSEARCH_DISABLE_SKELETON=selectionsort,heapsort,mergesort,binarysearch
EXTRA="--adjacent-nested 0.1 --range-shell 0.5 --partition 0.1 --start-size 55" scripts/run_sorting_seeds.sh "$OUT/A55" 1200 1006 1007 1008 1009 1010 >> "$OUT/driver.log" 2>&1
EXTRA="--adjacent-nested 0.3 --partition 0.1 --range-shell 0.05 --selection-block 0.1 --merge-shell 0.05" scripts/run_sorting_seeds.sh "$OUT/A0" 1200 1001 1002 1003 1004 1005 1006 1007 1008 >> "$OUT/driver.log" 2>&1
EXTRA="--adjacent-nested 0.3 --selection-block 0.3 --partition 0.1 --start-size 30" scripts/run_sorting_seeds.sh "$OUT/A30" 1200 1001 1002 1003 1004 >> "$OUT/driver.log" 2>&1
echo "SORTING_FAMILIES_BIG_DONE $(date +%T)" >> "$OUT/driver.log"
