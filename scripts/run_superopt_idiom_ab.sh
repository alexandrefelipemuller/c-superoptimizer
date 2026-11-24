#!/bin/bash
# Log 208: control vs --idiom-moves, matched seeds (10-12), 60 s, --time-top 4,
# one search at a time, arm order alternating per group.
#   A: planted set (tests/c_samples_planted{,_heldout}); B: earlier samples on
#   random inputs (regression check). Run detached: setsid nohup ... &
#   scripts/run_superopt_idiom_ab.sh OUTDIR [SEEDS] [SECONDS]
set -u
cd "$(dirname "$0")/.."
O=${1:?outdir}; SEEDS=${2:-"10 11 12"}; SECS=${3:-60}
M=tests/c_samples_musl_lua; R=tests/c_samples_real2
MR=$(ls $M/*.c | grep -vE 'musl_(isalpha|iscntrl|isspace|ascii_is_unsafe|ascii_to_bin|hexval)\.c' | tr '\n' ' ')
G="planted_dev|$(ls tests/c_samples_planted/*.c | tr '\n' ' ')
planted_heldout|$(ls tests/c_samples_planted_heldout/*.c | tr '\n' ' ')
samples|$(ls tests/c_samples/*.c tests/c_samples_set2/*.c | tr '\n' ' ')
m_random|$MR
r_random|$R/curl_smb_swap16.c $R/curl_smb_swap32.c $R/mbedtls_aes_rot_word.c $R/png_first_transform.c $R/png_is_combo.c"
mkdir -p "$O"
k=0
while IFS='|' read -r g samples; do
    if [ $((k % 2)) -eq 0 ]; then arms="control idiom"; else arms="idiom control"; fi
    for a in $arms; do
        x=""; [ "$a" = idiom ] && x="--idiom-moves"
        echo "$(date +%T) $g $a" >> "$O/run.log"
        SAMPLES="$samples" scripts/run_superopt_battery.sh "$O/$a/$g" "$SEEDS" "$SECS" 1 4 "$x" >> "$O/run.log" 2>&1
    done
    k=$((k + 1))
done <<< "$G"
echo done > "$O/ALL_DONE"
