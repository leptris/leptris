#!/usr/bin/env bash
# bump-version.sh — Bump taurus version.
# Source of truth: git tags (latest vX.Y.Z).
# Usage: .github/scripts/bump-version.sh [major|minor|patch|X.Y.Z]
#
# Adapted from the jemalloc fork's bump-version.sh
# (tamatebako/jemalloc/.github/scripts/bump-version.sh).
#
# Files updated:
#   - CMakeLists.txt  (project(taurus VERSION ...))
#   - vcpkg.json      (version-semver + version-string)
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
    perl -i -0777 -pe "s/(project\(\s*taurus\b[^)]*?)VERSION\s+[0-9]+\.[0-9]+\.[0-9]+/\${1}VERSION $NEXT/s" "$CMAKE_FILE"
    print_info "Updated CMakeLists.txt"
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
CURRENT_DATE=$(date "+Y-%m-%d" 2>/dev/null || date "+%Y-%m-%d")
TEMP_CL=$(mktemp)

cat > "$TEMP_CL" << EOF
## [Unreleased]

## [$NEXT] - $CURRENT_DATE

<!-- Edit this section with the actual release notes. -->
<!-- See https://keepachangelog.com for format guidance. -->

### Changed

- (describe changes here)

EOF

# Prepend to existing changelog (skip the top "## [Unreleased]" header)
if [ -f "$CHANGELOG" ]; then
    tail -n +2 "$CHANGELOG" >> "$TEMP_CL"
fi
mv "$TEMP_CL" "$CHANGELOG"
print_info "Updated CHANGELOG.md"

print_info ""
print_info "Version bump complete: $CURRENT_VERSION → $NEXT"
print_info "Files updated: CMakeLists.txt, vcpkg.json, CHANGELOG.md"
print_info "Next step: commit + push + open release PR"
