# TODO 12: Clean up compile warnings

**Priority**: P2 (hygiene)
**Status**: Planned
**Effort**: S

## Problem

`cmake --build build` emits 7 warnings under `-Wall -Wextra`:

```
src/taurus/memory/pool.h:249:47: warning: declaration of 'struct
    taurus_string_view' will not be visible outside of this function
    [-Wvisibility]

src/taurus/common/entities.h:12:35: warning: redefinition of typedef
    'TaurusMemoryPool' is a C11 feature [-Wtypedef-redefinition]
src/taurus/common/entities.h:13:26: warning: redefinition of typedef
    'TaurusDTD' is a C11 feature [-Wtypedef-redefinition]

src/taurus/parse/parser_new.c:1211:14: warning: unused variable
    'next_char' [-Wunused-variable]
src/taurus/parse/parser_new.c:1212:14: warning: unused variable
    'next_next_char' [-Wunused-variable]

src/taurus/parse/parser_new.c:122:12: warning: unused function
    'validate_utf8_string' [-Wunused-function]
```

## Root cause

Three distinct issues:

1. **Duplicate typedefs.** `TaurusMemoryPool` is typedef'd in three
   places (`memory/pool.h:71`, `common/entities.h:12`, `dtd/model.h` —
   struct tag decl in `model.h:15`). `TaurusDTD` is typedef'd in
   `common/entities.h:13` and again in `dtd/model.h:112`. C99 disallows
   redefinition; C11 allows it but the project is C99.
2. **Forward-declared struct used in a function prototype.**
   `pool.h:249` references `const struct taurus_string_view*` without
   including the header that defines it.
3. **Unused locals/functions.** Likely editing leftovers.

## Fix

### Single source of truth for typedefs

Create `src/taurus/common/types_internal.h`:

```c
#ifndef TAURUS_COMMON_TYPES_INTERNAL_H
#define TAURUS_COMMON_TYPES_INTERNAL_H

/* Forward declarations — single source of truth for internal typedefs. */
typedef struct taurus_memory_pool TaurusMemoryPool;
typedef struct taurus_document   TaurusDocument;
typedef struct TaurusDTD          TaurusDTD;
typedef struct TaurusElement_     *TaurusElement;
/* ... etc for every type used across module boundaries ... */

#endif
```

Every internal header includes this first. The duplicate typedefs in
`memory/pool.h`, `common/entities.h`, and `dtd/model.h` are removed.
The struct definitions stay where they are; only the typedefs move.

### Forward declaration for `TaurusStringView`

`pool.h:249` should include `common/string_view.h` (which defines
`TaurusStringView`), not forward-declare an incomplete struct. Since
this is a header in the same module, that's fine.

### Remove or use the dead code

- `parser_new.c:1211-1212`: read the surrounding code. If the variables
  were meant to gate a parser decision and the logic was removed,
  delete the variables. If they're needed for a TODO, add a comment
  with `(void)` cast to silence the warning until they're used.
- `parser_new.c:122: validate_utf8_string`: if utf8proc is the
  designated validator, delete this function. If it's a fallback for
  when utf8proc is disabled, mark it `__attribute__((unused))` or wrap
  in `#ifndef TAURUS_HAS_UTF8PROC`.

## Tests

No behavioral change expected. Re-run all existing specs and confirm
they pass.

## Architecture notes

Duplicate typedefs are a textbook DRY violation: the same fact ("this
type is named X") lives in three places. When (not if) one of those
definitions needs to change, the others go stale silently.

Centralizing internal typedefs in one header also makes the
"module-boundary" rule enforceable: anything that needs to know about
`TaurusMemoryPool` includes `types_internal.h`, which doesn't expose
struct fields. Callers can't poke at fields they shouldn't see.

## Verification

1. `cmake --build build 2>&1 | grep -c warning` returns **0**.
2. All specs pass.
3. `grep -rn "typedef struct taurus_memory_pool" src/taurus/` returns
   exactly one line (in `types_internal.h`).
