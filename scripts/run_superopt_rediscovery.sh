#!/bin/bash
# Section 187: can the search rediscover each C sample from its oracle alone
# (--problem c:FILE, no seed program)? Every confirmed solution is emitted as
# C and checked against the original compiled with gcc -O2 -fwrapv.
#   scripts/run_superopt_rediscovery.sh OUTDIR [SEEDS] [WALL] [JOBS]
set -u
OUT=${1:?outdir}; SEEDS=${2:-"2 3 4"}; WALL=${3:-240}; JOBS=${4:-3}
mkdir -p "$OUT"
one() {
    f=$1; seed=$2; out=$3; wall=$4
    s=$(basename "$f" .c); tag="$s.seed$seed"
    timeout $((wall + 120)) ./algosearch --problem "c:$f" --max-n 30 --beam-width 500 --candidates 1500 \
        --max-program-size 150 --seed "$seed" --restart-on-stall 6 --wall-seconds "$wall" --threads 2 \
        --output "$out/$tag.json" --c-out "$out/$tag.found.c" > "$out/$tag.log" 2>&1
    rc=$?
    if [ -f "$out/$tag.found.c" ]; then
        check=$(python3 scripts/c_roundtrip.py "$f" --candidate "$out/$tag.found.c" --workdir "$out/rt_$tag" | head -1)
        case "$check" in OK*) verdict=EQUIVALENT ;; *) verdict=NOT_EQUIVALENT ;; esac
        size=$(grep -oE 'size [0-9]+; reference size [0-9]+' "$out/$tag.log" | head -1)
    else
        verdict=NONE; check=""; size=""
    fi
    level=$(grep -E '^(arguments in|N = )' "$out/$tag.log" | tail -1)
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$s" "$seed" "$rc" "$verdict" "$size" "$level" "$check" >> "$out/summary.tsv"
}
export -f one
for f in tests/c_samples/*.c; do for seed in $SEEDS; do echo "$f $seed"; done; done |
    xargs -P "$JOBS" -n 2 bash -c 'one "$0" "$1" "'"$OUT"'" "'"$WALL"'"'
sort "$OUT/summary.tsv" -o "$OUT/summary.tsv"
echo done > "$OUT/DONE"
