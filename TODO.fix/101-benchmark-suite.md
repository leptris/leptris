# TODO 101 — comprehensive benchmark suite vs libxml2 / pugixml

**Priority**: P1 (visibility into perf claims)
**Status**: Phase 1 shipped (resource tracking + repair + SAX baseline)

## What was wrong

1. **Broken build**: `benchmarks/CMakeLists.txt` referenced files that
   no longer existed (`workflows/bench_dom_pugixml.cpp`,
   `dom_benchmark.cpp`, `comprehensive_benchmark.cpp`) — every target
   was unreachable.
2. **Bit-rotted bench files**: `bench_node_creation.c` used the
   `_fast` node-create variants TODO 18 collapsed; `benchmark_parse_ops.cpp`
   used the pre-rename `taurus_element_text_content`.  None of it built.
3. **Harness gaps**: only wall-clock time.  The user explicitly asked
   for CPU and RAM tracking — without them, "is taurus faster?" was
   unanswerable from the suite.

## Phase 1 (this PR)

### Harness extension (`benchmarks/common/benchmark.{h,c}`)
* `BenchResult` gains `cpu_mean_us`, `rss_peak_kb`, `ops_per_sec`,
  `mb_per_sec`.
* New `bench_set_payload_size_kb(double)` lets each benchmark hint
  its per-iteration payload size for throughput reporting.
* CPU time via `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` (fallback
  to `clock()` on platforms without POSIX timers).
* Peak RSS via `getrusage(RUSAGE_SELF).ru_maxrss`, normalized to KB
  across Linux and macOS (different units).
* `bench_print_comparison` reports wall / cpu / rss ratios side by
  side.
* `bench_write_json` serializes every field so downstream tooling
  can diff runs.

### Build repair (`benchmarks/CMakeLists.txt`)
* Rewritten to wire the actual on-disk layout (dom/, xpath/,
  comprehensive/, serialization/, modification/, sax/).
* `taurus_add_benchmark` helper standardizes include + link setup.
* Modern pugixml imported target (`pugixml::pugixml`) preferred;
  falls back to `find_path` / `find_library` for older installs.
* libxml2 detection handles both `find_package(LibXml2)` and
  manual path search.
* Drive-by fixes: `bench_node_creation.c` include paths corrected
  (file is still bit-rotted re: `_fast` APIs and is excluded until
  rewritten); `benchmark_parse_ops.cpp` updated to
  `taurus_element_text` (renamed); `dom_benchmark_v2` links `utils.c`.

### SAX baseline (new: `benchmarks/sax/`)
* `bench_taurus.c` and `bench_libxml2.c` exercise the SAX reader
  with the same payload (small in-memory + medium in-memory +
  large file), same callback set, same iteration counts. Directly
  comparable.

### Sample numbers (local macOS run)

```
Taurus SAX
  SAX small                    mean  16.17 us  cpu  15.54 us  rss  1712 KB   54.3 MB/s
  SAX medium                   mean  88.25 us  cpu  73.19 us  rss  2032 KB   62.5 MB/s

libxml2 SAX
  SAX small                    mean   5.54 us  cpu   5.18 us  rss  2640 KB  158.5 MB/s
  SAX medium                   mean  17.62 us  cpu  17.20 us  rss  3168 KB  313.3 MB/s
```

libxml2 is currently 3-5x faster on SAX, and taurus uses ~35% less
RSS.  Concrete numbers — not vibes — are what makes optimization
tractable.

## Phase 2 (next)

* **CI workflow** runs the benchmark binaries on every PR, posts a
  Markdown summary comment with the side-by-side table, fails on
  > 10% regression vs main.
* **Modification benchmark vs pugixml** — currently taurus-only.
  The modification dir has scaffolding (`bench_dom_pugixml.cpp`
  was referenced by the old CMakeLists but doesn't exist on disk);
  write it.
* **Rewrite `bench_node_creation.c`** to use the post-TODO-18
  single entry-point API.
* **Aggregated reporter**: one script that runs every bench binary,
  parses the JSON output, and emits a single
  `benchmarks/results/<timestamp>.md` for archival.

## Phase 3 (later)

* **XPath coverage** — taurus has 6 bench files in `xpath/` but
  libxml2 has 3.  Fill the matrix so every axis has both sides.
* **Memory profile under sustained load** — RSS-over-time, not just
  peak.  Needs `mach_task_basic_info` on macOS and `/proc/self/statm`
  on Linux.
* **Pressure testing** — generate pathological inputs (deep nesting,
  wide attributes, huge CDATA) and bench them.
