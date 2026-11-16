#!/bin/bash
# Log 198: the original against itself, the best hand version known
# (tests/c_samples_musl_lua/hand/) and the search's three winners of its
# latest valid run, per sample, c_speedup.py --inline on 65,536 inputs that do
# not repeat within a pass (random, or *_eval_large.txt from
# make_char_workloads.py OUT --large 65536).
#     scripts/run_ceiling_battery.sh OUT TAG    # writes OUT/ceiling_TAG.tsv
#     ONLY=REGEX scripts/run_ceiling_battery.sh OUT TAG   # only the matching samples
set -u
OUT=$1; TAG=$2; H=tests/c_samples_musl_lua/hand
printf 'sample\tworkload\tvariant\tinline_speedup\tline\n' > "$OUT/ceiling_$TAG.tsv"
while read s src ev; do
  [[ $s =~ ${ONLY:-.} ]] || continue
  f=tests/c_samples_musl_lua/$s.c
  EV="--inputs 65536"; [ "$ev" != random ] && EV="$EV --inputs-file $OUT/${ev}_eval_large.txt"
  for v in self hand 10 11 12; do
    case $v in self) c=$f;; hand) c=$H/$s.c;; *) c=data/$src/$s.seed$v.found.c;; esac
    line=$(python3 scripts/c_speedup.py "$f" "$c" --inline $EV --workdir "$OUT/w_${s}_$v" 2>&1 | tail -1)
    sp=$(echo "$line" | grep -oE 'speedup [0-9.]+' | cut -d' ' -f2)
    printf '%s\t%s\t%s\t%s\t%s\n' "$s" "$ev" "$v" "$sp" "$line" >> "$OUT/ceiling_$TAG.tsv"
  done
done <<'L'
musl_isalpha superopt_confirm_text_20261115 text
musl_iscntrl superopt_confirm_text_20261115 text
musl_isspace superopt_confirm_text_20261115 text
musl_ascii_is_unsafe superopt_confirm_b64_20261115 b64
musl_ascii_to_bin superopt_confirm_b64_20261115 b64
musl_hexval superopt_workload_hex_20261115 hex
musl_checkint superopt_unsigned2_20261115 random
musl_isdigit superopt_unsigned2_20261115 random
musl_isgraph superopt_unsigned2_20261115 random
musl_islower superopt_unsigned2_20261115 random
musl_isprint superopt_unsigned2_20261115 random
musl_isupper superopt_unsigned2_20261115 random
musl_lcg31 superopt_unsigned2_20261115 random
musl_rol superopt_unsigned2_20261115 random
musl_ror superopt_unsigned2_20261115 random
musl_setup_salt superopt_unsigned2_20261115 random
musl_swapc superopt_unsigned2_20261115 random
musl_temper superopt_unsigned2_20261115 random
musl_zeroinfnan superopt_unsigned2_20261115 random
musl_isblank superopt_char_20261115 random
lua_digit superopt_char_20261115 random
musl_abs superopt_musl_lua2_20261115 random
musl_days_in_month superopt_musl_lua2_20261115 random
musl_is_leap superopt_musl_lua2_20261115 random
musl_isascii superopt_musl_lua2_20261115 random
musl_toascii superopt_musl_lua2_20261115 random
lua_filterpc superopt_musl_lua2_20261115 random
L
touch "$OUT/DONE_$TAG"
