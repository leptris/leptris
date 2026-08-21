#!/bin/bash
# scripts/check_shared_exports.sh — verify public symbols are exported.
#
# The library is built with CMAKE_C_VISIBILITY_PRESET=hidden.  Public
# entry points must carry LEPTRIS_API; otherwise the symbol is missing
# from the .so / .dylib export table and FFI bindings cannot dlsym it.
#
# SAX was missing LEPTRIS_API through v0.3.0 (TODO 122).  This script
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
    -DLEPTRIS_BUILD_SHARED=ON \
    -DLEPTRIS_BUILD_STATIC=OFF \
    -DBUILD_TESTING=OFF \
    -DLEPTRIS_BUILD_CLI=OFF \
    -DLEPTRIS_BUILD_BENCHMARKS=OFF \
    > /dev/null

cmake --build "$BUILD" --target leptris_shared > /dev/null

LIB=$(find "$BUILD" -name 'libleptris.*.dylib' -o -name 'libleptris.so*' \
        2>/dev/null | grep -v '[0-9]\.dylib$' | head -1)
# Fall back to the unversioned symlink if the versioned name didn't match.
[ -z "$LIB" ] && LIB=$(find "$BUILD" -name 'libleptris.dylib' -o -name 'libleptris.so' | head -1)

if [ -z "$LIB" ]; then
    echo "FAIL: shared library not built" >&2
    exit 1
fi

echo "Checking exports in: $LIB"

# Symbols that must be exported for FFI bindings (Ruby / Python / Rust).
REQUIRED=(
    leptris_parse_string
    leptris_document_root
    leptris_document_free
    leptris_document_encoding
    leptris_element_name
    leptris_element_text
    leptris_element_attribute
    leptris_element_attribute_count
    leptris_element_attribute_name_at
    leptris_element_attribute_value_at
    leptris_element_child_count
    leptris_element_child
    leptris_element_parent
    leptris_element_namespace_count
    leptris_xpath_eval
    leptris_xpath_result_count
    leptris_xpath_result_type
    leptris_xpath_result_get
    leptris_xpath_result_boolean
    leptris_xpath_result_number
    leptris_xpath_result_string
    leptris_xpath_result_free
    leptris_status_string
    leptris_document_serialize
    leptris_element_serialize
    leptris_document_save_file
    leptris_text_node_create
    leptris_comment_node_create
    leptris_cdata_node_create
    leptris_pi_node_create
    leptris_text_node_set_content
    leptris_comment_node_set_content
    leptris_cdata_node_set_content
    leptris_pi_node_set_target
    leptris_pi_node_set_data
    leptris_node_parent
    leptris_node_unlink
    leptris_node_line
    leptris_node_compare
    leptris_element_namespace_decl_prefix
    leptris_element_namespace_decl_uri
    leptris_c14n_canonicalize_subtree
    leptris_c14n_canonicalize_ex
    leptris_c14n_canonicalize_subtree_ex
    leptris_element_add_namespace_definition
    leptris_element_set_default_namespace
    leptris_element_remove_namespace_definition
    leptris_xpath_eval_with_vars_context
    leptris_sax_parse
    leptris_sax_parser_create
    leptris_sax_parser_feed
    leptris_sax_parser_free
    leptris_sax_parser_set_streaming
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
    echo "FAIL: some public symbols not exported (LEPTRIS_API missing?)" >&2
    exit $rc
fi

echo "All required symbols exported."
