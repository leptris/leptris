# archive/

Historical, disabled, or superseded source files.  **Not part of the
active build** — see `src/CMakeLists.txt` `TAURUS_SOURCES` for the
authoritative list of files that are compiled into libtaurus.

Files were moved here (not deleted) per the project rule "NEVER DELETE
source files."  Git history is preserved; the active tree under `src/`
is clean.

## Layout

```
archive/
├── parse_legacy/              Old parser implementations, superseded by
│                              src/taurus/parse/parser_new.c.
│   ├── taurus_parse.c
│   ├── parse_content.c
│   ├── parse_document.c
│   ├── parse_simple.c
│   ├── parse_element.c
│   ├── parse_helpers.{c,h}
│   └── parse_internal.h
│
├── dtd_validator_disabled/    DTD content-model validation, not finished.
│   └── validator.c            "Phase 5/6" per the original CMakeLists comment.
│
├── xinclude_disabled/         XInclude support, predates the compact
│   ├── xinclude.c             architecture rewrite; needs rework before
│   └── xinclude.h             it can be re-enabled.
│
└── backups/
    └── evaluator_axes.c.bak2  A manual backup that leaked into the tree.
```

## Why these are here

See [`TODO.fix/21-archive-legacy-exec.md`](../TODO.fix/21-archive-legacy-exec.md)
and the earlier [`TODO.fix/13-cleanup-dead-code.md`](../TODO.fix/13-cleanup-dead-code.md).

In short: every file under `archive/` was either:

1. **Disabled at the CMake level** (commented out of `TAURUS_SOURCES`).
2. **Superseded** by a newer implementation (the active file lives under
   `src/`).
3. **A backup artifact** that should never have been committed.

## Re-activating a file

If you want to revive one of these (e.g., finish the DTD validator),
move it back to its canonical location under `src/` and add it to
`TAURUS_SOURCES` in `src/CMakeLists.txt`.  The file's git history will
follow naturally.
