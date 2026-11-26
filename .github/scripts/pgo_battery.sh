#!/usr/bin/env bash
#
# PGO training + published-row battery (#2167).
#
# One SSOT for both phases of the two-phase PGO dance: the rows we
# publish ARE the training set. Every binary here runs a FIXED
# workload (fixed iterations/reps constants in the binaries — no
# wall-time-adaptive loops), which is what makes the emitted profile
# deterministic and the C3-style build-twice gate meaningful.
#
# Modes:
#   TRAIN — run each binary once against the GENERATE build.
#       clang: LLVM_PROFILE_FILE lands per-PID .profraw files in
#           <build-dir>/pgo-data (merge with llvm-profdata afterwards —
#           the workflow step does that per-OS).
#       gcc:   .gcda written at process exit directly into the
#           -fprofile-generate dir (no merge).
#       msvc:  .pgc written at clean process exit beside each .pgd
#           (merge with pgomgr afterwards).
#   ROWS  — capture each binary's stdout to <out-dir>/<name>.txt so
#           pgo_gate.py can build the USE-vs-OFF delta table.
#
# usage: pgo_battery.sh <repo-root> <build-dir> <mode> <toolchain> <out-dir> [--allow-missing]
#
#   --allow-missing  windows legs lack pugixml/libxml2, so part of the
#       promise set does not build there; the leg trains on the
#       leptris-only subset instead of failing.
set -euo pipefail

repo="$1"; build="$2"; mode="$3"; toolchain="$4"; out="$5"; shift 5
allow_missing=0
for a in "$@"; do
  [ "$a" = "--allow-missing" ] && allow_missing=1
done

case "$mode" in train|rows) ;; *) echo "pgo_battery: mode must be train|rows" >&2; exit 2 ;; esac

bench_dir="$build/benchmarks"
mkdir -p "$bench_dir/results" "$out"
# Canonicalize $out BEFORE the run loop: the row-mode redirect evaluates
# $out INSIDE the subshell that already cd'd into build/benchmarks, so a
# relative path would resolve against the wrong tree (the first proof
# run lost every row to exactly that).
out="$(cd "$out" && pwd)"
if [ ! -d "$bench_dir/fixtures" ]; then
  cp -r "$repo/benchmarks/fixtures" "$bench_dir/fixtures"
fi

# The leptris rows only. Competitor binaries (bench_*_libxml2, pugixml
# sides) instrument nothing in OUR library — running them would pad the
# training time for zero profile signal.
bins="bench_dom_leptris bench_xpath_leptris bench_xpath_axes_leptris \
bench_xpath_functions_leptris bench_sax_leptris bench_serialize \
bench_modify_leptris benchmark_parse dom_benchmark_v2 bench_parse_ab \
bench_append_attr_pugixml bench_pipeline_shapes"

if [ "$mode" = "train" ] && [ "$toolchain" = "clang" ]; then
  # CMake hardcodes -fprofile-instr-generate=<dir>/default.profraw at
  # link time; every process would overwrite the same file. The env
  # override wins at runtime and gives one profraw per PID.
  export LLVM_PROFILE_FILE="$build/pgo-data/%p.profraw"
fi

# ASLR off for training on Linux: the library keeps address-keyed
# caches, and layout randomization flips the odd edge counter between
# otherwise identical training runs — exactly what the proof's
# build-twice gate caught (a single differing byte, then a differing
# member). A fixed layout pins the counters; ROWS mode stays
# unmodified because it measures performance, not profiles.
train_wrap=""
if [ "$mode" = "train" ] && [ "$(uname -s)" = "Linux" ] \
   && command -v setarch >/dev/null 2>&1; then
  train_wrap="setarch $(uname -m) -R"
fi

ran=0 missing=0
for name in $bins; do
  bin="$bench_dir/$name"
  if [ ! -x "$bin" ]; then
    missing=$((missing + 1))
    echo "pgo_battery: $name not built — skipped"
    continue
  fi
  if [ "$mode" = "train" ]; then
    # A deliberately malformed corpus file must not kill training.
    (cd "$bench_dir" && $train_wrap "./$name" > /dev/null 2>&1) \
      || echo "pgo_battery: $name exited nonzero (training tolerates)"
  else
    (cd "$bench_dir" && "./$name" > "$out/$name.txt" 2>&1) \
      || echo "pgo_battery: $name failed — see $out/$name.txt"
  fi
  ran=$((ran + 1))
done

if [ "$mode" = "rows" ] && [ "$allow_missing" != "1" ] && [ "$missing" != "0" ]; then
  echo "pgo_battery: $missing row binaries missing — the promise set must \
build for a row measurement (install pugixml/libxml2, or pass --allow-missing \
on the windows leg)" >&2
  exit 1
fi

echo "pgo_battery: mode=$mode toolchain=$toolchain ran=$ran missing=$missing"
