#!/usr/bin/env python3
"""Export-surface gate (TODO.concurrency/02).

Diffs the shared library's exported symbols (nm -gU) against the
LEPTRIS_API declarations across src/include/. Every exported symbol
must be declared public; every declared symbol must export. Catches
the internal-header-but-exported class (leptris_parse family before
the visibility fix, leptris_serialize_document before #468).

Usage: check_export_surface.py <shared-library> <repo-root>
Skips (exit 0) when the library path doesn't exist — callers wire
this against a shared build only.
"""
import re
import subprocess
import sys
from pathlib import Path


def declared_public_symbols(root: Path):
    text = ""
    for h in sorted((root / "src" / "include").rglob("*.h")):
        text += h.read_text() + "\n"
    # Strip comments so commented-out decls don't count.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    names = set()
    for m in re.finditer(
        r"LEPTRIS_API[^;{}]*?\b(leptris_[a-z_0-9]+)\s*\(", text, flags=re.S
    ):
        names.add(m.group(1))
    return names


def exported_symbols(lib: Path):
    out = subprocess.run(
        ["nm", "-gU", str(lib)], capture_output=True, text=True, check=True
    ).stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] in ("T", "B", "D"):
            name = parts[-1]
            # Mach-O symbols carry a leading underscore.
            if name.startswith("_"):
                name = name[1:]
            if name.startswith("leptris_"):
                names.add(name)
    return names


def main():
    lib = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/src/libleptris.dylib")
    root = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(".")
    if not lib.exists():
        print(f"EXPORT GATE: skip ({lib} not built)")
        return 0

    declared = declared_public_symbols(root)
    if len(declared) < 100:
        print(f"EXPORT GATE: header parse looks wrong ({len(declared)} symbols)")
        return 1

    exported = exported_symbols(lib)

    undeclared = sorted(exported - declared)
    missing = sorted(declared - exported)

    failures = []
    for n in undeclared:
        failures.append(f"UNDECLARED EXPORT {n}: exported but absent from src/include/")
    for n in missing:
        failures.append(f"MISSING EXPORT   {n}: declared public but not exported")

    if failures:
        print("EXPORT SURFACE DRIFT:")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"Export surface clean: {len(exported)} exported == {len(declared)} declared")
    return 0


if __name__ == "__main__":
    sys.exit(main())
