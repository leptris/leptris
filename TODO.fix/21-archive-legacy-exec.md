# TODO 21: Archive legacy and disabled code

**Priority**: P3 (hygiene)
**Status**: Planned — execute Option A from TODO 13
**Effort**: S

## Problem

The active source tree contains files that are not in the build. They
confuse readers and grease up greps. Full inventory is in
[13-cleanup-dead-code.md](13-cleanup-dead-code.md).

Per the global rule "NEVER DELETE source files," we move rather than
remove. Git history is preserved; the active tree becomes clean.

## Plan

Move the files below into `archive/` mirroring their current paths.
Add an `archive/README.md` explaining what's there and why.

```
archive/
├── README.md
├── parse_legacy/
│   ├── taurus_parse.c                (was: src/taurus/taurus_parse.c)
│   ├── parse_content.c               (was: src/taurus/parse_content.c)
│   ├── parse_document.c              (was: src/taurus/taurus_parse/parse_document.c — wait, actually src/taurus/parse_document.c)
│   ├── parse_simple.c
│   ├── parse_element.c
│   ├── parse_helpers.{c,h}
│   └── parse_internal.h
├── dtd_validator_disabled/
│   └── validator.c                   (was: src/taurus/dtd/validator.c)
├── xinclude_disabled/
│   ├── xinclude.c
│   └── xinclude.h
├── dom_legacy/
│   ├── element_compact.c             (if not in active build)
│   └── element_fast.c                (if not in active build)
└── backups/
    └── evaluator_axes.c.bak2         (was: src/taurus/xpath/evaluator_axes.c.bak2)
```

Confirm each file is NOT in `TAURUS_SOURCES` (`src/CMakeLists.txt`)
before moving. If a file IS in the active build, leave it alone and
note the discrepancy.

## After the move

Add to root `CMakeLists.txt`:

```cmake
# Historical / disabled code lives under archive/. Not built.
# See archive/README.md and TODO.fix/21-archive-legacy.md.
```

And to `.gitignore`:

```
/archive/*/build/
```

(nothing else — archive/ is committed; only its build artifacts are
ignored if someone experimentally builds there.)

## Tests

No behavioral change. `cmake --build build` should still succeed;
`ctest --test-dir build` should still be 100%.

## Verification

```bash
git mv src/taurus/taurus_parse.c archive/parse_legacy/taurus_parse.c
# ... etc for each file
cmake --build build
ctest --test-dir build
# All green.
```

The active source tree (`src/`) contains only files in `TAURUS_SOURCES`.

## Architectural notes

Moving (not deleting) respects the global rule. The active tree
becomes navigable: every file under `src/` is real and built.
Historical code is reachable in `archive/` for考古.
