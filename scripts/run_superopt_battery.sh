#!/bin/bash
# Sections 188/188b: superoptimize every C sample from its reference (algosearch
# superopt), then, serially (timing needs a quiet machine), check each result
# against the compiled original and measure its compiled speedup.
#   scripts/run_superopt_battery.sh OUTDIR [SEEDS] [SECONDS] [JOBS]
set -u
OUT=${1:?outdir}; SEEDS=${2:-"1 2 3"}; SECS=${3:-60}; JOBS=${4:-4}; TIME_TOP=${5:-0}; EXTRA=${6:-}
# SAMPLES (environment): the C files, default tests/c_samples/*.c. EXTRA:
# further superopt flags, e.g. --algorithmic-moves (section 190).
SAMPLES=${SAMPLES:-tests/c_samples/*.c}
# EVAL_INPUTS (environment, log 195): time the external measurements on this
# workload file (c_speedup.py --inputs-file) instead of random inputs.
EV=""; [ -n "${EVAL_INPUTS:-}" ] && EV="--inputs-file $EVAL_INPUTS"
# RT_INPUTS (environment, log 199): run the equivalence round trip on this
# file instead of random inputs (strings: random arrays rarely hold a NUL).
RT=""; [ -n "${RT_INPUTS:-}" ] && RT="--inputs-file $RT_INPUTS"
# TIME_TOP > 0 (section 189): compiled timing inside the loop. The searches
# then time code themselves, so they run with JOBS=1 (one at a time).
mkdir -p "$OUT"
one() {
    f=$1; seed=$2; out=$3; secs=$4; ttop=$5; more=$6
    s=$(basename "$f" .c); tag="$s.seed$seed"
    extra=""
    [ "$ttop" -gt 0 ] && extra="--time-top $ttop --work-dir $out/w_$tag"
    timeout $((secs * 4 + 300)) ./algosearch superopt "$f" --seconds "$secs" --seed "$seed" --out "$out/$tag.found.c" \
        $extra $more > "$out/$tag.log" 2>&1
    rm -rf "$out/w_$tag"
    echo "rc $?" >> "$out/$tag.log"
}
export -f one
for f in $SAMPLES; do for seed in $SEEDS; do echo "$f $seed"; done; done |
    xargs -P "$JOBS" -n 2 bash -c 'one "$0" "$1" "'"$OUT"'" "'"$SECS"'" "'"$TIME_TOP"'" "'"$EXTRA"'"'
printf 'sample\tseed\tverified\tref_cost\tbest_cost\tmodel_speedup\tref_size\tbest_size\tcounterexamples\tequivalence\tspeedup\tby_order\tref_ns\tcand_ns\tinline_speedup\tinline_ref_ns\tloop_measured\tloop_self\tloop_noise\tmodel_best_cost\tequivalence_level\texhaustive_rejected\tuser_workload\tfuzz_rejected\n' > "$OUT/summary.tsv"
for f in $SAMPLES; do for seed in $SEEDS; do
    s=$(basename "$f" .c); tag="$s.seed$seed"; log="$OUT/$tag.log"
    get() { grep -m1 "^$1 " "$log" | cut -d' ' -f2; }
    eq=NONE; sp=""; bo=""; rn=""; cn=""; isp=""; irn=""
    if [ -s "$OUT/$tag.found.c" ]; then
        chk=$(python3 scripts/c_roundtrip.py "$f" --candidate "$OUT/$tag.found.c" $RT --workdir "$OUT/rt_$tag" | head -1)
        case "$chk" in OK*) eq=EQUIVALENT ;; *) eq=NOT_EQUIVALENT ;; esac
        m=$(python3 scripts/c_speedup.py "$f" "$OUT/$tag.found.c" $EV --workdir "$OUT/sp_$tag")
        sp=$(echo "$m" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
        bo=$(echo "$m" | grep -oE 'by_order [0-9./]+' | cut -d' ' -f2)
        rn=$(echo "$m" | grep -oE 'ref_ns [0-9.]+' | cut -d' ' -f2)
        cn=$(echo "$m" | grep -oE 'cand_ns [0-9.]+' | cut -d' ' -f2)
        mi=$(python3 scripts/c_speedup.py "$f" "$OUT/$tag.found.c" --inline $EV --workdir "$OUT/spi_$tag")
        isp=$(echo "$mi" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
        irn=$(echo "$mi" | grep -oE 'ref_ns [0-9.]+' | cut -d' ' -f2)
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$s" "$seed" "$(get verified)" "$(get reference_cost)" \
        "$(get best_cost)" "$(get model_speedup)" "$(get reference_size)" "$(get best_size)" "$(get counterexamples)" \
        "$eq" "$sp" "$bo" "$rn" "$cn" "$isp" "$irn" "$(get measured_speedup)" "$(get self_control)" \
        "$(get noise_threshold)" "$(get model_best_cost)" "$(get equivalence_level)" "$(get exhaustive_rejected)" "$(get user_workload)" "$(get fuzz_rejected)" >> "$OUT/summary.tsv"
done; done
# External self-control per sample (the original against itself), both modes.
printf 'sample\tself_call\tself_inline\n' > "$OUT/self_control.tsv"
for f in $SAMPLES; do
    s=$(basename "$f" .c)
    a=$(python3 scripts/c_speedup.py "$f" "$f" $EV --workdir "$OUT/self_$s" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
    b=$(python3 scripts/c_speedup.py "$f" "$f" --inline $EV --workdir "$OUT/selfi_$s" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
    printf '%s\t%s\t%s\n' "$s" "$a" "$b" >> "$OUT/self_control.tsv"
done
echo done > "$OUT/DONE"
