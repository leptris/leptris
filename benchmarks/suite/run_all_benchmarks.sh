#!/bin/bash
#
# Taurus Comprehensive Benchmark Suite - Unified Runner
#
# Runs all benchmarks and produces a comprehensive summary.
# Target: >= 1.0x vs pugixml for DOM operations, >= 1.0x vs libxml2 for XPath
#
# Usage:
#   ./run_all_benchmarks.sh [--quick] [--category=NAME]
#
# Options:
#   --quick       Run with reduced iterations for fast development
#   --category    Run only benchmarks for a specific category
#                 (parsing, traversal, attributes, modification, xpath, scenarios, memory, serialize)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../build"
DATA_DIR="${SCRIPT_DIR}/../fixtures/data"
QUICK_MODE=0
CATEGORY=""
FAILED_TESTS=()
PASSED_COUNT=0
FAILED_COUNT=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            QUICK_MODE=1
            shift
            ;;
        --category=*)
            CATEGORY="${1#*=}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--quick] [--category=NAME]"
            exit 1
            ;;
    esac
done

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print header
echo ""
echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║           TAURUS COMPREHENSIVE BENCHMARK SUITE                          ║${NC}"
echo -e "${BLUE}║   Target: >= 1.0x vs pugixml (DOM), >= 1.0x vs libxml2 (XPath)         ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ $QUICK_MODE -eq 1 ]; then
    echo -e "${YELLOW}Running in QUICK mode (reduced iterations for development)${NC}"
    echo ""
fi

# Check if fixtures exist
if [ ! -d "$DATA_DIR" ]; then
    echo -e "${RED}Error: Fixture data directory not found: $DATA_DIR${NC}"
    echo "Please run: ./build/benchmarks/fixtures/generate_fixtures"
    exit 1
fi

# Check build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo "Please run: cmake -B build -S . -DTAURUS_BUILD_BENCHMARKS=ON && cmake --build build"
    exit 1
fi

# Function to run a benchmark and parse results
run_benchmark() {
    local name=$1
    local executable=$2
    local args=$3

    echo ""
    echo -e "${CYAN}════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  $name${NC}"
    echo -e "${CYAN}════════════════════════════════════════════════════════════════════════${NC}"

    if [ ! -x "$executable" ]; then
        echo -e "${YELLOW}SKIP: Executable not found: $executable${NC}"
        return 0
    fi

    local output
    local exit_code=0
    output=$($executable $args "$DATA_DIR" 2>&1) || exit_code=$?

    echo "$output"

    # Count pass/fail from output
    local pass_count fail_count
    pass_count=$(echo "$output" | grep -c "PASS" || true)
    fail_count=$(echo "$output" | grep -c "FAIL" || true)

    PASSED_COUNT=$((PASSED_COUNT + pass_count))
    FAILED_COUNT=$((FAILED_COUNT + fail_count))

    if [ $fail_count -gt 0 ]; then
        # Extract failed test names
        while IFS= read -r line; do
            if [[ "$line" == *"FAIL"* ]] && [[ "$line" == *"Speedup"* || "$line" == *"vs"* ]]; then
                FAILED_TESTS+=("$name: $line")
            fi
        done <<< "$output"
    fi

    return 0
}

# Run benchmarks by category
run_all_benchmarks() {
    # Parsing benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "parsing" ]; then
        run_benchmark "XML PARSING (10 tests)" "${BUILD_DIR}/benchmarks/suite/bench_parsing"
    fi

    # Traversal benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "traversal" ]; then
        run_benchmark "DOM TRAVERSAL (5 tests)" "${BUILD_DIR}/benchmarks/suite/bench_traversal"
    fi

    # Attribute benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "attributes" ]; then
        run_benchmark "ATTRIBUTE ACCESS (12 tests)" "${BUILD_DIR}/benchmarks/suite/bench_attributes"
    fi

    # Modification benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "modification" ]; then
        run_benchmark "DOM MODIFICATION (8 tests)" "${BUILD_DIR}/benchmarks/suite/bench_modification"
    fi

    # XPath benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "xpath" ]; then
        run_benchmark "XPATH ALL AXES + FUNCTIONS (20 tests)" "${BUILD_DIR}/benchmarks/suite/bench_xpath_all"
    fi

    # Scenario benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "scenarios" ]; then
        run_benchmark "REAL-WORLD SCENARIOS (4 tests)" "${BUILD_DIR}/benchmarks/suite/bench_scenarios"
    fi

    # Memory benchmarks
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "memory" ]; then
        if [ -x "${BUILD_DIR}/benchmarks/suite/bench_memory" ]; then
            run_benchmark "MEMORY EFFICIENCY (4 tests)" "${BUILD_DIR}/benchmarks/suite/bench_memory"
        fi
    fi

    # Serialization benchmarks (if exists)
    if [ -z "$CATEGORY" ] || [ "$CATEGORY" = "serialize" ]; then
        if [ -x "${BUILD_DIR}/benchmarks/suite/bench_serialize" ]; then
            run_benchmark "SERIALIZATION (8 tests)" "${BUILD_DIR}/benchmarks/suite/bench_serialize"
        fi
    fi
}

# Run all benchmarks
run_all_benchmarks

# Calculate totals
TOTAL_COUNT=$((PASSED_COUNT + FAILED_COUNT))

# Print summary
echo ""
echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                      BENCHMARK SUITE SUMMARY                            ║${NC}"
echo -e "${BLUE}╠════════════════════════════════════════════════════════════════════════╣${NC}"
printf "${BLUE}║${NC}  Total Tests:   %6d                                                 ${BLUE}║${NC}\n" "$TOTAL_COUNT"
printf "${BLUE}║${NC}  ${GREEN}Passed:        %6d${NC}                                                 ${BLUE}║${NC}\n" "$PASSED_COUNT"
printf "${BLUE}║${NC}  ${RED}Failed:        %6d${NC}                                                 ${BLUE}║${NC}\n" "$FAILED_COUNT"
if [ $TOTAL_COUNT -gt 0 ]; then
    PASS_RATE=$(awk "BEGIN {printf \"%.1f\", $PASSED_COUNT * 100 / $TOTAL_COUNT}")
    printf "${BLUE}║${NC}  Pass Rate:     %6s%%                                               ${BLUE}║${NC}\n" "$PASS_RATE"
fi
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════════╝${NC}"

# Print failed tests
if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed Tests (need optimization):${NC}"
    printf "${RED}══════════════════════════════════════════════════════════════════════${NC}\n"
    for test in "${FAILED_TESTS[@]:0:20}"; do  # Limit to 20 for readability
        echo -e "  ${RED}✗${NC} $test"
    done
    if [ ${#FAILED_TESTS[@]} -gt 20 ]; then
        echo "  ... and $(( ${#FAILED_TESTS[@]} - 20 )) more failures"
    fi
fi

# Print performance targets reminder
echo ""
echo -e "${CYAN}Performance Targets:${NC}"
echo "  - Parsing:      >= 0.33x vs pugixml (within 3x), >= 1.0x vs libxml2"
echo "  - Traversal:    >= 1.0x vs pugixml"
echo "  - Attributes:   >= 1.0x vs pugixml (O(1) lookup for many attrs)"
echo "  - Modification: >= 1.0x vs pugixml (parity)"
echo "  - XPath:        >= 1.0x vs libxml2 for all axes/functions"
echo "  - Memory:       <= 110% vs pugixml"
echo ""

# Exit with appropriate code
if [ $FAILED_COUNT -gt 0 ]; then
    echo -e "${YELLOW}Some benchmarks failed performance targets.${NC}"
    echo -e "${YELLOW}See results above for details on what needs optimization.${NC}"
    echo ""
    exit 1
else
    echo -e "${GREEN}All benchmarks passed performance targets!${NC}"
    echo ""
    exit 0
fi
