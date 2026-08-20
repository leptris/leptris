# Benchmark Matrix (`bench_matrix` + `bench2html.rb`)

Full-feature comparison matrix: taurus vs pugixml vs libxml2, measuring
**latency** (min/median µs), **throughput** (MB/s), **CPU** (user/sys ms) and
**peak RSS** (KB) across every supported capability.

Not every library supports every feature (pugixml has no SAX; libxml2's DOM
model differs) — unsupported cells are omitted from that library's YAML and
rendered as `—` in the HTML report.

## Covered features

| Feature | taurus | pugixml | libxml2 |
|---|---|---|---|
| DOM parse (4 shapes) | ✓ | ✓ | ✓ |
| SAX parse (4 shapes) | ✓ | — | ✓ |
| Serialize (2 shapes) | ✓ | ✓ | — |
| Mutation (append, set-attr) | ✓ | ✓ | — |
| XPath (3 queries) | ✓ | ✓ | ✓ |

## Usage

```bash
# 1. Build with all optional dependencies found
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DTAURUS_BUILD_BENCHMARKS=ON
cmake --build build --target bench_matrix

# 2. Run — produces one YAML per library
mkdir -p build/bench-matrix
./build/benchmarks/matrix/bench_matrix build/bench-matrix

# 3. Generate HTML report
ruby benchmarks/matrix/bench2html.rb build/bench-matrix/*.yaml \
    --output build/bench_report.html

# 4. Open the report
open build/bench_report.html    # macOS
```

## YAML format

Each library gets one file (`taurus.yaml`, `pugixml.yaml`, `libxml2.yaml`):

```yaml
meta:
  library: 'taurus'
  version: '0.25.11'
  timestamp: '2026-08-20T10:00:00'
  platform: 'macOS-arm64'
benchmarks:
  - id: 'dom_parse_text-heavy-2MB'
    feature: 'DOM parse'
    input: 'text-heavy-2MB'
    size_bytes: 2047744
    iterations: 20
    metrics:
      latency_us:
        min: 199.0
        median: 205.0
      throughput_mbs: 10.30
      cpu_user_ms: 3.98
      cpu_sys_ms: 0.45
      memory_peak_kb: 3244
```

## HTML report

The generated report contains:
- a full comparison matrix (latency µs + throughput MB/s per cell, winner highlighted)
- a resource-usage table (peak RSS + CPU user/sys)
- bar charts (Chart.js) per benchmark shape
