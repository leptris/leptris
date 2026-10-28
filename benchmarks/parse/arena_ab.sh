#!/bin/sh
# arena_ab.sh — interleaved A/B for parse-arena sizing changes
# (leptris#1846 discipline: same-process-pair, order-alternating,
# median of per-pair ratios — cross-wave deltas are NOT a valid
# referee, the 2026-10-25 wave swung both directions at noise
# magnitude).
#
# Usage: arena_ab.sh <base-build-dir> <patch-build-dir> <fixture> [pairs]
# Both build dirs must contain benchmarks/parse_ab (the file-mode
# bench_parse_ab). Each pair runs BASE then PATCH, then PATCH then
# BASE (ABBA), so thermal/load drift cancels; the reported number is
# the median of patch/base MB/s ratios.
set -e
BASE=$(ls "$1"/benchmarks/parse_ab "$1"/benchmarks/bench_parse_ab 2>/dev/null | head -1)
PATCH=$(ls "$2"/benchmarks/parse_ab "$2"/benchmarks/bench_parse_ab 2>/dev/null | head -1)
FIX="$3"
N="${4:-10}"
if [ -z "$BASE" ] || [ -z "$PATCH" ] || [ -z "$FIX" ]; then
    echo "usage: $0 <base-build-dir> <patch-build-dir> <fixture> [pairs]" >&2
    exit 1
fi
run_one() {
    "$1" "$FIX" | awk '{print $4}'
}
ratios=""
i=0
while [ "$i" -lt "$N" ]; do
    a=$(run_one "$BASE"); b=$(run_one "$PATCH")
    b2=$(run_one "$PATCH"); a2=$(run_one "$BASE")
    ratios="$ratios
$(echo "$b $a" | awk '{printf "%.4f", $1/$2}')
$(echo "$b2 $a2" | awk '{printf "%.4f", $1/$2}')"
    i=$((i + 1))
done
echo "$ratios" | sort -n | awk '
    { v[NR] = $1 }
    END {
        m = (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2
        printf "pairs=%d median patch/base = %.4fx\n", NR, m
    }'
