#!/usr/bin/env python3
"""PGO USE-vs-OFF row gate (#2167).

Reads the two ROWS-mode result dirs written by pgo_battery.sh
(off-dir/use-dir, each holding run<k>/<name>.txt), extracts ONE scalar
per row binary with a direction-aware rule, takes the least-noise
extreme across runs (min for us/ns rows, max for MB/s rows), and fails
when any leptris row regresses beyond the tolerance.

Tolerance default: 5%. Shared-runner noise between two build trees is
real (the perf-gate uses medians + 15% for exactly that reason); PGO
gains on this library are 1-20%, so a genuine regression is loud even
at 5%. The table always records the raw numbers for the PR body.

Output: one Markdown table on stdout (row | metric | OFF | USE | delta |
verdict), exit 1 iff any row FAILs.
"""

import argparse
import glob
import os
import re
import sys

# Per-binary extraction rule: (regex, aggregation, direction).
#   aggregation: "sum" | "mean"
#   direction:   "lower" (us/ns — smaller is faster) | "higher" (MB/s)
# Default rule: the shared C harness's bench_print_summary "total cpu"
# lines — CPU time, summed across every section of the binary.
DEFAULT_RULE = (r"total cpu:\s*([\d.]+)", "sum", "lower")
RULES = {
    "bench_dom_leptris": DEFAULT_RULE,
    "bench_xpath_leptris": DEFAULT_RULE,
    "bench_xpath_axes_leptris": DEFAULT_RULE,
    "bench_xpath_functions_leptris": DEFAULT_RULE,
    "bench_sax_leptris": DEFAULT_RULE,
    "bench_serialize": DEFAULT_RULE,
    "bench_modify_leptris": DEFAULT_RULE,
    # C++ flagship rows have their own line formats. Note benchmark_parse
    # indents its comparison lines — no line-start anchor there.
    "dom_benchmark_v2": (r"Leptris:\s+([\d.]+)", "sum", "lower"),
    "benchmark_parse": (r"Leptris:\s+([\d.]+)", "sum", "lower"),
    "bench_pipeline_shapes": (r"engine=leptris median_us=([\d.]+)",
                              "sum", "lower"),
    "bench_append_attr_pugixml": (r"leptris[^:]*:\s*([\d.]+)",
                                  "sum", "lower"),
    "bench_parse_ab": (r"^\S+\s+\d+\s+([\d.]+)\s+[\d.]+\s+MB/s",
                       "mean", "higher"),
}

METRIC_NAME = {"lower": "us/ns (lower)", "higher": "MB/s (higher)"}


def scalar(text, rule):
    """Aggregate all regex matches of one result file into one scalar."""
    pattern, agg, _direction = rule
    values = [float(m) for m in re.findall(pattern, text, re.MULTILINE)]
    if not values:
        raise ValueError("no matches for " + pattern)
    return sum(values) if agg == "sum" else sum(values) / len(values)


def row_value(result_dir, name):
    """Least-noise extreme across run<k>/ dirs; None when the row is absent."""
    rule = RULES[name]
    files = sorted(glob.glob(os.path.join(result_dir, "run*", name + ".txt")))
    if not files:
        return None
    values = [scalar(open(f, encoding="utf-8").read(), rule) for f in files]
    return min(values) if rule[2] == "lower" else max(values)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("off_dir")
    ap.add_argument("use_dir")
    ap.add_argument("--tolerance", type=float, default=5.0)
    args = ap.parse_args()

    failed = False
    lines = [
        "### PGO row gate — USE vs OFF (per-row, least-noise of 3 runs)",
        "",
        "| row | metric | OFF | USE | delta | verdict |",
        "|---|---|---|---|---|---|",
    ]
    for name, rule in RULES.items():
        off = row_value(args.off_dir, name)
        use = row_value(args.use_dir, name)
        if off is None or use is None or off == 0:
            lines.append("| %s | %s | %s | %s | — | MISSING |"
                         % (name, METRIC_NAME[rule[2]], off, use))
            failed = True
            continue
        if rule[2] == "lower":
            delta = (use - off) / off * 100.0
        else:  # MB/s: regression means the throughput DROPPED
            delta = (off - use) / off * 100.0
        verdict = "FAIL" if delta > args.tolerance else "ok"
        if delta > args.tolerance:
            failed = True
        lines.append("| %s | %s | %.2f | %.2f | %+.2f%% | %s |"
                     % (name, METRIC_NAME[rule[2]], off, use, delta, verdict))

    lines += ["", "Gate tolerance: %.1f%% — any row slower (or lower "
                  "throughput) than that vs the unprofiled build fails "
                  "the job." % args.tolerance]
    print("\n".join(lines))
    if failed:
        print("pgo_gate: FAILED")
        sys.exit(1)
    print("pgo_gate: all rows ok")


if __name__ == "__main__":
    main()
