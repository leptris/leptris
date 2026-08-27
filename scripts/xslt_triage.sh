#!/usr/bin/env bash
# xslt_triage.sh — regenerate test/xslt/open_cases.txt from reality.
#
# Runs the libxslt-suite runner in triage mode (LEPTRIS_XSLT_TRIAGE=1):
# every open-list case executes fork-isolated and emits a one-line
# signature (bucket + first differing line pair). Cases that pass drop
# out of the list; cases that still fail stay in, so the list can
# never drift. POSIX-only (the fork isolation the mode relies on).
#
# Usage: scripts/xslt_triage.sh [path-to-test_libxslt_suite]
#        (default: newest build*/test/test_libxslt_suite)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIST="$ROOT/test/xslt/open_cases.txt"

BIN="${1:-}"
if [ -z "$BIN" ]; then
    for d in "$ROOT"/build*/test; do
        [ -x "$d/test_libxslt_suite" ] && BIN="$d/test_libxslt_suite"
    done
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "error: test_libxslt_suite not found — build it or pass the path" >&2
    exit 1
fi
echo "runner: $BIN"

OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

LEPTRIS_XSLT_TRIAGE=1 "$BIN" >"$OUT" 2>&1 || true

regressions=$(grep -cE '^\[  FAILED  \]' "$OUT" || true)
if [ "$regressions" -gt 0 ]; then
    echo "REGRESSIONS (non-open cases failing): $regressions" >&2
    grep -E '^\[  FAILED  \]' "$OUT" | head -20 >&2
    exit 1
fi

passing=$(grep -c '^TRIAGE-PASS ' "$OUT" || true)
open_total=$(grep -c '^TRIAGE ' "$OUT" || true)

echo
echo "=== triage: $((passing + open_total)) open-list cases ran," \
     "$passing now pass, $open_total still failing ==="
grep '^TRIAGE ' "$OUT" | awk '{print $3}' | sort | uniq -c | sort -rn
echo

# Regenerate the list with the still-failing bases.
tmp_list="$(mktemp)"
grep '^TRIAGE ' "$OUT" | awk '{print $2}' | sort >"$tmp_list"
if [ ! -s "$tmp_list" ]; then
    echo " Suite complete — no open cases remain."
    : >"$tmp_list"
fi
if ! diff -q "$tmp_list" "$LIST" >/dev/null 2>&1; then
    cp "$tmp_list" "$LIST"
    echo "open_cases.txt updated ($(wc -l <"$LIST" | tr -d ' ') open)."
else
    echo "open_cases.txt already current ($(wc -l <"$LIST" | tr -d ' ') open)."
fi
