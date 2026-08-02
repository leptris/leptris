/* lib/src/common/types_internal.h — single source of truth for internal
 * typedefs shared across libtaurus modules.
 *
 * The C standard (C99) disallows redefining a typedef in the same scope
 * (C11 relaxes this).  Without a central header, every internal module
 * that needed to reference TaurusMemoryPool / TaurusDTD ended up
 * re-typedef'ing them, producing a flood of -Wtypedef-redefinition
 * warnings and risking drift.  See TODO 12.
 *
 * Include this file FIRST in any internal header that needs these
 * types.  Do NOT redefine them locally.
 */

#ifndef TAURUS_COMMON_TYPES_INTERNAL_H
#define TAURUS_COMMON_TYPES_INTERNAL_H

/* Memory pool — full definition in memory/pool.h */
typedef struct taurus_memory_pool TaurusMemoryPool;

/* DTD container — full definition in dtd/model.h */
typedef struct TaurusDTD TaurusDTD;

/* Document opaque handle — pointer type, matches public types.h.
 * Do NOT redefine as `struct taurus_document` here; the public API
 * exposes `typedef struct taurus_document* TaurusDocument`. */

#endif /* TAURUS_COMMON_TYPES_INTERNAL_H */
