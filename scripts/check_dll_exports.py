#!/usr/bin/env python3
"""Windows DLL export-table audit (issue #430).

Diffs the export table of the built leptris.dll against the public
surface declared in src/include (every LEPTRIS_API declaration).
A declared-but-unexported symbol is a red X here instead of a
Ruby-side "attach_function failed" mystery.

Usage: check_dll_exports.py <path-to-leptris.dll> <repo-root>
"""

import re
import sys
from pathlib import Path

import pefile

API_RE = re.compile(r"LEPTRIS_API\s+[^(;]+?\b(leptris_[a-z_0-9]+)\s*\(", re.S)


def main():
    dll, root = sys.argv[1], Path(sys.argv[2])

    declared = set()
    for hdr in (root / "src" / "include").rglob("*.h"):
        text = re.sub(r"/\*.*?\*/", " ", hdr.read_text(errors="replace"), flags=re.S)
        declared.update(API_RE.findall(text))

    pe = pefile.PE(dll, fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    )
    exported = set()
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if sym.name:
                exported.add(sym.name.decode(errors="replace").lstrip("_"))

    missing = sorted(declared - exported)
    if missing:
        print(f"MISSING FROM {dll}:")
        for m in missing:
            print(f"  {m}")
        print(
            f"\n{len(missing)} of {len(declared)} declared public symbols are "
            "not exported — missing LEPTRIS_API on their definitions?"
        )
        return 1
    print(f"Export table clean: {len(declared)} declared symbols all exported.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
