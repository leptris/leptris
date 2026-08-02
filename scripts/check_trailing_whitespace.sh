#!/usr/bin/env bash
# Check for trailing whitespace in text source files.
# Excludes: docs, archive/, build dirs, binary fixtures.
# See TODO 86 (jemalloc-inspired checks).
set -euo pipefail

if git grep -nE '[[:space:]]+$' -- \
    '*.c' '*.h' '*.cpp' '*.sh' '*.yml' '*.cmake' '*.md' ':!*.adoc' \
    ':!archive/' \
    ':!docs/api-generated/' \
    ':!build/' ':!build-*/' \
    ':!benchmarks/fixtures/' \
    ':!ports/' \
    ':!CHANGELOG.md'; then
    echo "::error::Found trailing whitespace in source files (see above)"
    exit 1
fi
echo "OK: no trailing whitespace"
