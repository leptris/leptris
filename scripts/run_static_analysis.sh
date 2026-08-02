#!/usr/bin/env bash
# Run cppcheck static analysis. Informational only.
set -euo pipefail
if ! command -v cppcheck &>/dev/null; then
    echo "skip: cppcheck not installed"; exit 0
fi
ARGS=(--enable=warning,performance
      --suppress=missingInclude
      --inline-suppr -q)
if [ -f build/compile_commands.json ]; then
    ARGS+=(--project=build/compile_commands.json)
else
    ARGS+=(src/)
fi
cppcheck "${ARGS[@]}" 2>&1 || echo "(cppcheck found issues — informational)"
echo "OK: cppcheck complete"
