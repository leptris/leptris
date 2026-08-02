#!/usr/bin/env bash
# Run cppcheck static analysis on src/.
set -euo pipefail

if ! command -v cppcheck &>/dev/null; then
    echo "skip: cppcheck not installed"
    exit 0
fi

ARGS=(--enable=warning,performance --suppress=missingInclude
      --suppress=memleakOnRealloc
      --inline-suppr --error-exitcode=1 -q)

if [ -f build/compile_commands.json ]; then
    ARGS+=(--project=build/compile_commands.json)
else
    ARGS+=(src/)
fi

cppcheck "${ARGS[@]}"
echo "OK: cppcheck clean"
