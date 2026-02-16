#!/bin/bash
# run_benchmarks.sh - Run all Taurus benchmarks and save results
# Usage: ./scripts/run_benchmarks.sh [output_dir]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
OUTPUT_DIR="${1:-$PROJECT_DIR/benchmark_results}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== Taurus Benchmark Suite ==="
echo "Build Dir: $BUILD_DIR"
echo "Output Dir: $OUTPUT_DIR"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if benchmarks are built
if [ ! -d "$BUILD_DIR/benchmarks" ]; then
    echo -e "${RED}Error: Benchmarks not built. Run cmake first.${NC}"
    echo "  cmake -B build -S . -DTAURUS_BUILD_BENCHMARKS=ON"
    echo "  cmake --build build"
    exit 1
fi

# Get timestamp
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_FILE="$OUTPUT_DIR/benchmark_results_${TIMESTAMP}.txt"

echo "=== Benchmark Results ===" > "$RESULTS_FILE"
echo "Date: $(date)" >> "$RESULTS_FILE"
echo "Platform: $(uname -s) $(uname -m)" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# Function to run a benchmark and save output
run_benchmark() {
    local name=$1
    local executable=$2
    local args=${3:-}

    echo -e "${YELLOW}Running: $name${NC}"

    if [ -x "$executable" ]; then
        echo "" >> "$RESULTS_FILE"
        echo "=== $name ===" >> "$RESULTS_FILE"
        if $executable $args >> "$RESULTS_FILE" 2>&1; then
            echo -e "${GREEN}✓ Completed: $name${NC}"
        else
            echo -e "${RED}✗ Failed: $name (exit code: $?)${NC}"
            echo "FAILED" >> "$RESULTS_FILE"
        fi
    else
        echo -e "${RED}✗ Not found: $executable${NC}"
        echo "=== $name === SKIPPED (not built)" >> "$RESULTS_FILE"
    fi
}

# Run DOM benchmarks
echo ""
echo "--- DOM Benchmarks ---"
echo "" >> "$RESULTS_FILE"
echo "--- DOM Benchmarks ---" >> "$RESULTS_FILE"

run_benchmark "DOM Parse" "$BUILD_DIR/benchmarks/bench_dom_parse" "$PROJECT_DIR/benchmarks"

run_benchmark "DOM Traverse (medium)" "$BUILD_DIR/benchmarks/dom_benchmark_v2" "$PROJECT_DIR/benchmarks/fixtures/medium.xml"

run_benchmark "DOM Traverse (large)" "$BUILD_DIR/benchmarks/dom_benchmark_v2" "$PROJECT_DIR/benchmarks/fixtures/large.xml"

run_benchmark "DOM Modification" "$BUILD_DIR/benchmarks/bench_dom_pugixml"

# Run comprehensive benchmark
run_benchmark "Comprehensive" "$BUILD_DIR/benchmarks/comprehensive_benchmark"

echo ""
echo "=== Summary ==="
echo "Results saved to: $RESULTS_FILE"
echo ""

# Print summary of results
echo "--- Performance Summary ---"
grep -E "Speedup:|Average:" "$RESULTS_FILE" | head -20

echo ""
echo "Full results available at: $RESULTS_FILE"
