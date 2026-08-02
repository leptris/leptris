#!/usr/bin/env bash
# Check C code formatting with clang-format.
# Style file: .clang-format (Linux/Mac) or msvc/.clang-format (Windows).
# See TODO 86 (jemalloc-inspired checks).
set -euo pipefail

if ! command -v clang-format &>/dev/null; then
    echo "skip: clang-format not installed"
    exit 0
fi

# Files to check (active source only — no archive/, no build/).
FILES=$(find src cli -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "skip: no source files found"
    exit 0
fi

DIFF=$(clang-format --style=file --dry-run --Werror "${FILES[@]}" 2>&1 || true)
if [ -n "$DIFF" ]; then
    echo "::error::clang-format found formatting issues:"
    echo "$DIFF"
    echo ""
    echo "Run: clang-format -i ${FILES[*]}"
    exit 1
fi
echo "OK: clang-format clean on ${#FILES[@]} files"
