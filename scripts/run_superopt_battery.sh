#!/bin/bash
# Sections 188/188b: superoptimize every C sample from its reference (algosearch
# superopt), then, serially (timing needs a quiet machine), check each result
# against the compiled original and measure its compiled speedup.
#   scripts/run_superopt_battery.sh OUTDIR [SEEDS] [SECONDS] [JOBS]
set -u
OUT=${1:?outdir}; SEEDS=${2:-"1 2 3"}; SECS=${3:-60}; JOBS=${4:-4}
mkdir -p "$OUT"
one() {
    f=$1; seed=$2; out=$3; secs=$4
    s=$(basename "$f" .c); tag="$s.seed$seed"
    timeout $((secs * 4 + 300)) ./algosearch superopt "$f" --seconds "$secs" --seed "$seed" --out "$out/$tag.found.c" \
        > "$out/$tag.log" 2>&1
    echo "rc $?" >> "$out/$tag.log"
}
export -f one
for f in tests/c_samples/*.c; do for seed in $SEEDS; do echo "$f $seed"; done; done |
    xargs -P "$JOBS" -n 2 bash -c 'one "$0" "$1" "'"$OUT"'" "'"$SECS"'"'
printf 'sample\tseed\tverified\tref_cost\tbest_cost\tmodel_speedup\tref_size\tbest_size\tcounterexamples\tequivalence\tspeedup\tby_order\tref_ns\tcand_ns\tinline_speedup\tinline_ref_ns\n' > "$OUT/summary.tsv"
for f in tests/c_samples/*.c; do for seed in $SEEDS; do
    s=$(basename "$f" .c); tag="$s.seed$seed"; log="$OUT/$tag.log"
    get() { grep -m1 "^$1 " "$log" | cut -d' ' -f2; }
    eq=NONE; sp=""; bo=""; rn=""; cn=""; isp=""; irn=""
    if [ -s "$OUT/$tag.found.c" ]; then
        chk=$(python3 scripts/c_roundtrip.py "$f" --candidate "$OUT/$tag.found.c" --workdir "$OUT/rt_$tag" | head -1)
        case "$chk" in OK*) eq=EQUIVALENT ;; *) eq=NOT_EQUIVALENT ;; esac
        m=$(python3 scripts/c_speedup.py "$f" "$OUT/$tag.found.c" --workdir "$OUT/sp_$tag")
        sp=$(echo "$m" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
        bo=$(echo "$m" | grep -oE 'by_order [0-9./]+' | cut -d' ' -f2)
        rn=$(echo "$m" | grep -oE 'ref_ns [0-9.]+' | cut -d' ' -f2)
        cn=$(echo "$m" | grep -oE 'cand_ns [0-9.]+' | cut -d' ' -f2)
        mi=$(python3 scripts/c_speedup.py "$f" "$OUT/$tag.found.c" --inline --workdir "$OUT/spi_$tag")
        isp=$(echo "$mi" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
        irn=$(echo "$mi" | grep -oE 'ref_ns [0-9.]+' | cut -d' ' -f2)
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$s" "$seed" "$(get verified)" "$(get reference_cost)" \
        "$(get best_cost)" "$(get model_speedup)" "$(get reference_size)" "$(get best_size)" "$(get counterexamples)" \
        "$eq" "$sp" "$bo" "$rn" "$cn" "$isp" "$irn" >> "$OUT/summary.tsv"
done; done
echo done > "$OUT/DONE"
