#!/usr/bin/env bash
# Regenerate the public API reference with Doxygen.
#
# Doxygen is the documentation system for this C codebase: the public
# headers under src/include carry the contract comments (params,
# returns, and the "Memory:" ownership notes every binding builds
# on), and Doxygen renders them to docs/api-generated/html.
#
# The version is taken from CMakeLists.txt so the reference always
# matches the release it documents.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v doxygen >/dev/null 2>&1; then
    echo "doxygen not found (brew install doxygen / apt install doxygen)" >&2
    exit 1
fi

version=$(sed -n '/^project(leptris/,/)/{s/.*VERSION \([0-9][0-9.]*\).*/\1/p;}' CMakeLists.txt | head -1)
if [ -z "$version" ]; then
    echo "could not read version from CMakeLists.txt" >&2
    exit 1
fi

cd docs
{
    cat Doxyfile
    echo "PROJECT_NUMBER = ${version}"
} | doxygen -

echo "API reference written to docs/api-generated/html (version ${version})"
