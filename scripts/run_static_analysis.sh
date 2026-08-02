#!/usr/bin/env bash
# Run cppcheck static analysis on src/.
# See TODO 86 (jemalloc-inspired checks).
set -euo pipefail

if ! command -v cppcheck &>/dev/null; then
    echo "skip: cppcheck not installed"
    exit 0
fi

# Build arguments; cppcheck understands compile_commands.json if present.
ARGS=(--enable=warning,performance --suppress=missingInclude
      --inline-suppr --error-exitcode=1 -q)

if [ -f build/compile_commands.json ]; then
    ARGS+=(--project=build/compile_commands.json)
else
    ARGS+=(src/)
fi

cppcheck "${ARGS[@]}"
echo "OK: cppcheck clean"
