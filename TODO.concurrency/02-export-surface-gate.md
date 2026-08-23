# 02 — Exported-but-undeclared symbols: close the gap, gate it

User report: the internal `leptris_parse` family (src/leptris/
leptris_parse.h) exports from the dylib yet appears in no public
header — invisible to header-derived bindings. Root cause: non-Windows
builds use default visibility, exporting EVERY internal symbol; only
Windows is narrow (dllexport-only).

- Build: `C_VISIBILITY_PRESET hidden` on leptris_objects — internal
  symbols stay resolvable inside the final binary (tests, CLI embed
  the objects) but no longer export from the shared library.
  LEPTRIS_API (visibility default) overrides for the public surface.
- Decision on `leptris_parse` family: UN-EXPORT, do not promote — it
  returns `struct leptris_document*` and takes no status out-param;
  promoting would leak internal shapes. Bindings use
  `leptris_parse_string*` / `leptris_parse_file*`.
- New gate: scripts/check_export_surface.py — nm -gU on the shared
  library diffed against LEPTRIS_API declarations across
  src/include/. Wired as a ctest (skips when no shared lib) and a
  CI job with a shared build. Same class as the #468 retro-declared
  leptris_serialize_document.

DONE 2026-08-23: visibility preset + un-export + gate script + CI job;
gate diff is empty (every exported symbol is declared public).
