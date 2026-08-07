#!/bin/bash
# scripts/check_shared_exports.sh — verify public symbols are exported.
#
# The library is built with CMAKE_C_VISIBILITY_PRESET=hidden.  Public
# entry points must carry TAURUS_API; otherwise the symbol is missing
# from the .so / .dylib export table and FFI bindings cannot dlsym it.
#
# SAX was missing TAURUS_API through v0.3.0 (TODO 122).  This script
# builds a one-off shared library and asserts the SAX surface (and
# a few DOM/XPath canaries) appear in the export table.
#
# Registered as a CTest so it runs in CI alongside the spec suite.

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-export-check"

rm -rf "$BUILD"
cmake -B "$BUILD" -S "$ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTAURUS_BUILD_SHARED=ON \
    -DTAURUS_BUILD_STATIC=OFF \
    -DBUILD_TESTING=OFF \
    -DTAURUS_BUILD_CLI=OFF \
    -DTAURUS_BUILD_BENCHMARKS=OFF \
    > /dev/null

cmake --build "$BUILD" --target taurus_shared > /dev/null

LIB=$(find "$BUILD" -name 'libtaurus.*.dylib' -o -name 'libtaurus.so*' \
        2>/dev/null | grep -v '[0-9]\.dylib$' | head -1)
# Fall back to the unversioned symlink if the versioned name didn't match.
[ -z "$LIB" ] && LIB=$(find "$BUILD" -name 'libtaurus.dylib' -o -name 'libtaurus.so' | head -1)

if [ -z "$LIB" ]; then
    echo "FAIL: shared library not built" >&2
    exit 1
fi

echo "Checking exports in: $LIB"

# Symbols that must be exported for FFI bindings (Ruby / Python / Rust).
REQUIRED=(
    taurus_parse_string
    taurus_document_root
    taurus_document_free
    taurus_document_encoding
    taurus_element_name
    taurus_element_text
    taurus_element_attribute
    taurus_element_attribute_count
    taurus_element_attribute_name_at
    taurus_element_attribute_value_at
    taurus_element_child_count
    taurus_element_child
    taurus_element_parent
    taurus_element_namespace_count
    taurus_xpath_eval
    taurus_xpath_result_count
    taurus_xpath_result_type
    taurus_xpath_result_get
    taurus_xpath_result_boolean
    taurus_xpath_result_number
    taurus_xpath_result_string
    taurus_xpath_result_free
    taurus_status_string
    taurus_sax_parse
    taurus_sax_parser_create
    taurus_sax_parser_feed
    taurus_sax_parser_free
    taurus_sax_parser_set_streaming
)

rc=0
for sym in "${REQUIRED[@]}"; do
    # nm flags: -g externals only, -U exclude undefined, -C no demangle.
    # macOS:    nm -gU <lib>
    # Linux:    nm -D --defined-only <lib>
    if nm -gU "$LIB" 2>/dev/null | grep -qE " _?${sym}\$"; then
        echo "  OK   $sym"
    elif nm -D --defined-only "$LIB" 2>/dev/null | grep -qE " ${sym}\$"; then
        echo "  OK   $sym"
    else
        echo "  MISS $sym"
        rc=1
    fi
done

if [ $rc -ne 0 ]; then
    echo "FAIL: some public symbols not exported (TAURUS_API missing?)" >&2
    exit $rc
fi

echo "All required symbols exported."
