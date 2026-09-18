# Lever 5: #682 XSLT 2x — per-bench profile rounds

Status: TODO. Gate: every XSLT bench ≥2x libxslt and Saxon.
Lane is in progress; the remaining gap benches need individual
profiling rounds (perf record on the specific bench, top-5
self-time, one lever each).

Rounds:
1. List benches below 2x from the last scorecard run.
2. For each: perf record, identify the top self-time function,
   land ONE lever, re-measure.
Candidates seen in past rounds: pattern match linear scans,
output escaping per-byte, variable lookup re-walks.
