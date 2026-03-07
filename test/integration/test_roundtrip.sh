#!/bin/bash
# Test that parsing preserves all characters

BUILD_DIR="build"
FIXTURES_DIR="test/fixtures/libxml2"
TAURUS="$BUILD_DIR/cli/taurus"

echo "=== Round-Trip Character Preservation Test ==="
echo

failed=0
total=0

for file in "$FIXTURES_DIR"/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        
        # Skip non-XML files (README, etc)
        case "$filename" in
            README*|*.md|*.txt|*.adoc)
                continue
                ;;
        esac
        
        total=$((total + 1))
        
        # Count non-whitespace characters in original
        orig_chars=$(cat "$file" | tr -d '[:space:]' | wc -c | tr -d ' ')
        
        # Count non-whitespace characters in parsed output  
        parsed_chars=$("$TAURUS" parse "$file" 2>&1 | tr -d '[:space:]' | wc -c | tr -d ' ')
        
        if [ "$orig_chars" -eq "$parsed_chars" ]; then
            echo "✓ $filename: $orig_chars chars"
        else
            echo "✗ $filename: LOST $(($orig_chars - $parsed_chars)) chars ($orig_chars → $parsed_chars)"
            failed=$((failed + 1))
        fi
    fi
done

echo
echo "================================"
echo "Total: $total XML files"
echo "Passed: $((total - failed))"
echo "Failed: $failed"
echo "================================"

exit $failed
