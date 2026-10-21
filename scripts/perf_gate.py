#!/usr/bin/env python3
"""Performance tripwire: compare the day's benchmark medians against
the committed baseline. Fails when any tracked metric regresses by
more than the recorded tolerance. Runner noise on shared CI is
significant; medians of 5 runs plus a generous default tolerance keep
this a regression tripwire, not a stopwatch."""
import json, os, re, statistics, sys

runs_path, baseline_path = sys.argv[1], sys.argv[2]
update = os.environ.get("UPDATE_BASELINE") == "true"

# dom_benchmark_v2 summary lines: "Leptris:  12.34 µs (N iterations)"
# grouped by the preceding "=== <test name> ===" header.
pat = re.compile(r"Leptris:\s*([0-9.]+)\s*µs")
hdr = re.compile(r"=== (.+?) ===")
today = {}
test = "unnamed"
for line in open(runs_path):
    h = hdr.search(line)
    if h:
        test = h.group(1).strip()
        continue
    m = pat.search(line)
    if m:
        today.setdefault(test, []).append(float(m.group(1)))
if not today:
    sys.exit("perf-gate: no 'ns/node' metrics found in the run output")
medians = {k: statistics.median(v) for k, v in today.items()}

base = {}
if os.path.exists(baseline_path):
    base = json.load(open(baseline_path))

if update or not base:
    json.dump(medians, open(baseline_path, "w"), indent=2, sort_keys=True)
    print("perf-gate: baseline recorded:", medians)
    sys.exit(0)

fail = False
for k, m in sorted(medians.items()):
    b = base.get(k)
    if b is None:
        print(f"perf-gate: {k}: new metric, median {m:.1f} ns/node recorded")
        continue
    regress = (m - b) / b * 100.0
    tol = base.get("__tolerance_pct__", 15.0)
    verdict = "FAIL" if regress > tol else "ok"
    print(f"perf-gate: {k}: median {m:.1f} vs baseline {b:.1f} ({regress:+.1f}%) {verdict}")
    if regress > tol:
        fail = True
sys.exit(1 if fail else 0)
