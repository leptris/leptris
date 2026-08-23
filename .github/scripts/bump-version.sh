#!/usr/bin/env bash
# bump-version.sh — Bump leptris version.
# Source of truth: git tags (latest vX.Y.Z).
# Usage: .github/scripts/bump-version.sh [major|minor|patch|X.Y.Z]
#
# Adapted from the jemalloc fork's bump-version.sh
# (tamatebako/jemalloc/.github/scripts/bump-version.sh).
#
# Files updated:
#   - CMakeLists.txt  (project(leptris VERSION ...))
#   - vcpkg.json      (version-semver + version-string)
#   - bindings/python/pyproject.toml (pyleptris, lockstep)
#   - CHANGELOG.md    (new entry template)
#
# The script does NOT commit or tag — the caller (release workflow)
# handles that.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CMAKE_FILE="$REPO_ROOT/CMakeLists.txt"
VCPKG_JSON="$REPO_ROOT/vcpkg.json"
CHANGELOG="$REPO_ROOT/CHANGELOG.md"

print_info()  { echo -e "\033[0;32m[INFO]\033[0m $1"; }
print_error() { echo -e "\033[0;31m[ERROR]\033[0m $1"; }

if [ $# -ne 1 ]; then
    print_error "Usage: $0 [major|minor|patch|X.Y.Z]"
    exit 1
fi

BUMP_TYPE="$1"

# Read current version from git tags
CURRENT_VERSION=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo "")

if [ -z "$CURRENT_VERSION" ]; then
    CURRENT_VERSION=$(grep -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE" | head -1 | awk '{print $2}')
fi

if [ -z "$CURRENT_VERSION" ]; then
    print_error "Could not determine current version"
    exit 1
fi

print_info "Current version: $CURRENT_VERSION"

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT_VERSION"

case "$BUMP_TYPE" in
    major) NEXT="$((MAJOR + 1)).0.0" ;;
    minor) NEXT="$MAJOR.$((MINOR + 1)).0" ;;
    patch) NEXT="$MAJOR.$MINOR.$((PATCH + 1))" ;;
    *)
        if [[ "$BUMP_TYPE" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            NEXT="$BUMP_TYPE"
        else
            print_error "Invalid bump type: $BUMP_TYPE"
            exit 1
        fi
        ;;
esac

print_info "New version: $NEXT"

# --- Update CMakeLists.txt ---
# project() is multi-line; use perl -0777 to match across lines.
if [ -f "$CMAKE_FILE" ]; then
    perl -i -0777 -pe "s/(project\(\s*leptris\b[^)]*?)VERSION\s+[0-9]+\.[0-9]+\.[0-9]+/\${1}VERSION $NEXT/s" "$CMAKE_FILE"
    print_info "Updated CMakeLists.txt"
fi

# --- Update bindings/python/pyproject.toml (pyleptris lockstep) ---
PYPROJECT="$REPO_ROOT/bindings/python/pyproject.toml"
if [ -f "$PYPROJECT" ]; then
    sed -i.bak 's/^version = "[^"]*"/version = "'"$NEXT"'"/' "$PYPROJECT"
    rm -f "$PYPROJECT.bak"
    print_info "Updated bindings/python/pyproject.toml"
fi

# --- Update vcpkg.json ---
if [ -f "$VCPKG_JSON" ]; then
    sed -i.bak "s/\"version-semver\": \"[^\"]*\"/\"version-semver\": \"$NEXT\"/" "$VCPKG_JSON"
    sed -i.bak "s/\"version-string\": \"[^\"]*\"/\"version-string\": \"$NEXT\"/" "$VCPKG_JSON"
    sed -i.bak "s/\"version\": \"[^\"]*\"/\"version\": \"$NEXT\"/" "$VCPKG_JSON"
    rm -f "$VCPKG_JSON.bak"
    print_info "Updated vcpkg.json"
fi

# --- Update CHANGELOG.md ---
CURRENT_DATE=$(date "+%Y-%m-%d")
TEMP_CL=$(mktemp)

# Draft the section from conventional commits since the last tag
# (feat -> Added, fix -> Fixed, perf -> Performance). The release PR
# is still the review gate — refine there if a one-line subject
# loses nuance.
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || true)
LOG_RANGE="${LAST_TAG:+$LAST_TAG..}HEAD"

type_bullets() {
    git log --format='%s' $LOG_RANGE 2>/dev/null \
        | awk -v t="$1" '
            match($0, "^" t "(\\([^)]*\\))?!?: ") {
                pre = substr($0, 1, RLENGTH)
                sub("^" t, "", pre)
                gsub(/[()!: ]/, "", pre)
                rest = substr($0, RLENGTH + 1)
                if (pre != "") print "- " rest " (" pre ")"
                else print "- " rest
            }'
}

FEAT=$(type_bullets feat)
FIX=$(type_bullets fix)
PERF=$(type_bullets perf)

NOTES=$(mktemp)
{
    if [ -n "$FEAT" ]; then echo "### Added";   echo; printf '%s\n' "$FEAT"; echo; fi
    if [ -n "$FIX" ];  then echo "### Fixed";  echo; printf '%s\n' "$FIX";  echo; fi
    if [ -n "$PERF" ]; then echo "### Performance"; echo; printf '%s\n' "$PERF"; echo; fi
} > "$NOTES"

if [ ! -s "$NOTES" ]; then
    printf '%s\n' \
        '<!-- Edit this section with the actual release notes. -->' \
        '<!-- See https://keepachangelog.com for format guidance. -->' \
        '' \
        '### Changed' \
        '' \
        '- (describe changes here)' > "$NOTES"
fi

{
    echo "## [Unreleased]"
    echo
    echo "## [$NEXT] - $CURRENT_DATE"
    echo
    cat "$NOTES"
    echo
} > "$TEMP_CL"
rm -f "$NOTES"

# Prepend to existing changelog (skip the top "## [Unreleased]" header)
if [ -f "$CHANGELOG" ]; then
    tail -n +2 "$CHANGELOG" >> "$TEMP_CL"
fi
mv "$TEMP_CL" "$CHANGELOG"
print_info "Updated CHANGELOG.md"

print_info ""
print_info "Version bump complete: $CURRENT_VERSION → $NEXT"
print_info "Files updated: CMakeLists.txt, bindings/python/pyproject.toml, vcpkg.json, CHANGELOG.md"
print_info "Next step: commit + push + open release PR"
