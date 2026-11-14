#!/bin/bash
# Section 190: A/B of the phase 3c algorithmic moves, both arms with compiled
# timing in the loop (--time-top 4), matched seeds, one search at a time.
#   scripts/run_superopt_moves_ab.sh OUTDIR [SEEDS] [SECONDS]
set -u
OUT=${1:?outdir}; SEEDS=${2:-"10 11 12"}; SECS=${3:-60}
export SAMPLES="tests/c_samples/*.c tests/c_samples_set2/*.c"
scripts/run_superopt_battery.sh "$OUT/off" "$SEEDS" "$SECS" 1 4 "--no-algorithmic-moves"
scripts/run_superopt_battery.sh "$OUT/on" "$SEEDS" "$SECS" 1 4 "--algorithmic-moves"
echo done > "$OUT/DONE"
