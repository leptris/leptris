#!/usr/bin/env bash
# Check C code formatting with clang-format.
# Style file: .clang-format (project root).
# Currently informational only — the codebase isn't fully clang-formatted yet.
set -euo pipefail

if ! command -v clang-format &>/dev/null; then
    echo "skip: clang-format not installed"
    exit 0
fi

FILES=$(find src cli -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)
if [ -z "$FILES" ]; then
    echo "skip: no source files found"
    exit 0
fi

# shellcheck disable=SC2086
DIFF=$(clang-format --style=file $FILES 2>&1 || true)
if [ -n "$DIFF" ]; then
    echo "::warning::clang-format found formatting issues (informational):"
    echo "$DIFF" | head -20
    echo "(Not failing CI — formatting migration is a separate task.)"
fi
echo "OK: clang-format check complete (informational)"
