#!/usr/bin/env bash
# Triage (8 in parallel, depth cap 1000) and family tally of a sorting-families run (sections 171-172).
# Usage: scripts/sorting_families_report.sh output_root
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?output_root}"
export TRIAGE_MAX_DEPTH=1000
ls -d "$OUT"/*/seed_* | while read -r d; do [ -s "$d/triage.json" ] || echo "$d"; done \
  | xargs -P 8 -I{} sh -c 'python3 scripts/triage_sorting.py {} > /dev/null 2>&1'
for arm in "$OUT"/*/; do echo "== $(basename "$arm")"; python3 scripts/sort_families.py "$arm" | tail -1; done
