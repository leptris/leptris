#!/usr/bin/env bash
set -euo pipefail
if ! command -v cppcheck &>/dev/null; then
    echo "skip: cppcheck not installed"; exit 0
fi
ARGS=(--enable=warning,performance
      --suppress=missingInclude
      --suppress=memleakOnRealloc
      --suppress=knownConditionTrueFalse
      --suppress=identicalConditionAfterEarlyExit
      --suppress=constVariablePointer
      --suppress=unreadVariable
      --suppress=variableScope
      --suppress=shadowVariable
      --inline-suppr --error-exitcode=1 -q)
if [ -f build/compile_commands.json ]; then
    ARGS+=(--project=build/compile_commands.json)
else
    ARGS+=(src/)
fi
cppcheck "${ARGS[@]}"
echo "OK: cppcheck clean"
