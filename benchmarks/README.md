# Taurus Performance Benchmarks

This directory contains performance benchmarks comparing Taurus against industry-leading XML libraries:
- **libxml2**: De facto XPath standard (used in Chrome, Firefox, Python lxml)
- **pugixml**: Fastest C++ DOM parser (zero-copy, excellent cache locality)

## Goals

**Phase 10 Mission**: Make Taurus the fastest XML library in C
- **≥1.2x faster than pugixml** in DOM operations
- **≥1.5x faster than libxml2** in XPath queries

## Directory Structure

```
benchmarks/
├── common/          # Shared utilities
│   ├── benchmark.h  # Benchmark harness API
│   ├── benchmark.c  # Timing and statistics
│   ├── test_data.h  # Test XML data declarations
│   └── test_data.c  # Embedded XML strings
├── dom/             # DOM operation benchmarks
│   ├── bench_taurus.c    # Taurus DOM tests
│   ├── bench_pugixml.cpp # pugixml DOM tests
│   └── bench_libxml2.c   # libxml2 DOM tests
└── xpath/           # XPath query benchmarks
    ├── bench_taurus.c    # Taurus XPath tests
    └── bench_libxml2.c   # libxml2 XPath tests
```

## Test Data

Three XML files represent realistic workloads:
- **Small** (~1KB): Micro-benchmarks, simple documents
- **Medium** (~10KB): Typical XML documents
- **Large** (~100KB): Stress testing, complex documents

## Building

```bash
# Enable benchmarks during configure
cmake -B build -S .. \
  -DTAURUS_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=Release

# Build all benchmarks
cmake --build build
```

## Running Benchmarks

```bash
cd build/benchmarks

# DOM benchmarks
./bench_dom_taurus
./bench_dom_pugixml    # if pugixml available
./bench_dom_libxml2    # if libxml2 available

# XPath benchmarks
./bench_xpath_taurus
./bench_xpath_libxml2  # if libxml2 available
```

## Benchmark Design Principles

1. **Fair Comparison**: Same XML, same operations, same iterations
2. **Realistic Workloads**: Representative of actual usage patterns
3. **Statistical Rigor**: Multiple runs, calculate mean/stddev/min/max
4. **Apples-to-Apples**: Measure equivalent operations only
5. **MECE Structure**: Each benchmark has single, clear purpose

## DOM Benchmarks

Tests core DOM operations:
1. **Parse + Root**: `parse(xml) + get_root()`
2. **Tree Traversal**: Depth-first walk of entire tree
3. **Attribute Access**: Get attribute by name (repeated)
4. **Text Extraction**: Get text content (repeated)
5. **Child Iteration**: Iterate all children (repeated)

## Decomposition Benchmark (`benchmark_decomp`)

Isolates WHERE parse time goes by holding document size constant while
varying node density (and vice versa). This is the benchmark that
answers "is the gap time or throughput?" — run it before concluding
anything from mixed-shape numbers.

| Probe | Shape | What it isolates |
|---|---|---|
| P1 | one element, ~2MB entity-free text | pure text-streaming throughput (zero-copy views vs in-situ copying) |
| P2 | 100k tiny `<a/>` | per-element cost: scan, struct stores, wiring |
| P3 | 50k attributes (5k elements x 10) | per-attribute cost: the name/`=`/value sub-scans + struct fields |
| P4 | 25k `<i>ab</i>` | per-text-node cost |
| P5 | P2's shape scaled 117KB -> 937KB | size-scaling curve: separates fixed/setup costs and cache-footprint effects from per-node costs |
| P6 | pretty-printed (`  <a/>\n`) | whitespace-only text nodes (one per element) |
| P6b | P6 with `TAURUS_PARSE_DROP_WS_TEXT` | pugixml-default whitespace semantics |

Readings (2026-08-19/20, Apple Silicon, min-of-30, pugixml anchored
in-process): text streaming **taurus wins ~3x** (zero-copy views);
per-element and per-text-node **parity**; attributes **~1.5x** (the
one structural gap); the small-document ratio is ~1.6x falling to
~1.2x by ~1MB — a cache-footprint effect; P6 pretty-ws ~1.6-1.7x
default, **~1.35x with DROP_WS** (the corpus gap's hidden driver).

Method note: ratios here are only comparable within one process
run; re-run both sides together, take minima, and prefer medians
when the machine is loaded.

## SAX Benchmark (`benchmark_sax`)

SAX-mode parse: taurus events vs libxml2 SAX2 (pugixml has no SAX interface,
so libxml2 is the streaming reference). No-op handlers on both sides — the
measurement is pure scan + callback dispatch with no tree construction.

Readings (2026-08-20, Apple Silicon, min-of-30): taurus wins every corpus
shape — small 0.40x, xsdtest 0.77x, large 0.92x, workflow 0.94x, catalog
0.93x. (Before round 14 removed a per-element malloc+free from the element
frame, large/catalog ran at 1.03-1.10x — a comment claiming the allocator
was "under 1% of parse time" was off by an order of magnitude; always
measure.)

## XPath Benchmarks

Tests query performance:
1. **Simple Path**: `//book` (find all books)
2. **Predicate**: `//book[@id='1']` (find by attribute)
3. **Function**: `count(//book)` (count elements)
4. **Complex Query**: `//book[price > 30]/title` (predicate + navigation)
5. **Union**: `//book | //magazine` (union operator)

## Dependencies

**Required**:
- Taurus library (always built)

**Optional** (gracefully disabled if missing):
- libxml2 (via pkg-config)
- pugixml (via CMake find_path/find_library)

Install on macOS:
```bash
brew install libxml2 pugixml
```

Install on Ubuntu/Debian:
```bash
sudo apt-get install libxml2-dev libpugixml-dev
```

## Output Format

Each benchmark reports:
- **Mean**: Average time per iteration (microseconds)
- **Stddev**: Standard deviation
- **Min/Max**: Fastest and slowest runs
- **Speedup**: Comparison factor vs competitor

Example output:
```
Taurus DOM Benchmarks (Medium XML, 1000 iterations)
====================================================

Parse + Root:
  Mean:   12.34 µs
  Stddev:  0.56 µs
  Min:    11.80 µs
  Max:    15.20 µs

vs pugixml: 1.25x faster ✓
vs libxml2: 2.10x faster ✓
```

## Troubleshooting

**libxml2 not found**:
- Install via package manager
- Or disable with `-DTAURUS_BUILD_BENCHMARKS=OFF`

**pugixml not found**:
- Install via vcpkg: `vcpkg install pugixml`
- Or disable with `-DTAURUS_BUILD_BENCHMARKS=OFF`

**Benchmarks crash**:
- Verify XML is valid
- Check library API usage in benchmark code
- Add debug printf to isolate issue

## Performance Targets

### DOM Operations (vs pugixml)
- Parse + Root: ≥1.2x faster
- Tree Traversal: ≥1.2x faster
- Attribute Access: ≥1.2x faster
- Text Extraction: ≥1.2x faster
- Child Iteration: ≥1.2x faster

### XPath Queries (vs libxml2)
- Simple Paths: ≥1.5x faster
- Predicates: ≥1.5x faster
- Functions: ≥1.5x faster
- Complex Queries: ≥1.5x faster
- Union Operator: ≥1.5x faster

## Contributing

When adding new benchmarks:
1. Use the benchmark harness in `common/benchmark.h`
2. Follow MECE principles (one operation per test)
3. Use shared test data from `common/test_data.h`
4. Document expected performance characteristics
5. Update this README with new benchmarks
