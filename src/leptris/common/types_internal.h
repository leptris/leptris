/* lib/src/common/types_internal.h — single source of truth for internal
 * typedefs shared across libleptris modules.
 *
 * The C standard (C99) disallows redefining a typedef in the same scope
 * (C11 relaxes this).  Without a central header, every internal module
 * that needed to reference LeptrisMemoryPool / LeptrisDTD ended up
 * re-typedef'ing them, producing a flood of -Wtypedef-redefinition
 * warnings and risking drift.  See TODO 12.
 *
 * Include this file FIRST in any internal header that needs these
 * types.  Do NOT redefine them locally.
 */

#ifndef LEPTRIS_COMMON_TYPES_INTERNAL_H
#define LEPTRIS_COMMON_TYPES_INTERNAL_H

/* Memory pool — full definition in memory/pool.h */
typedef struct leptris_memory_pool LeptrisMemoryPool;

/* DTD container — full definition in dtd/model.h.  Guard matches the
 * public dtd.h so the two headers can be included in either order. */
#ifndef LEPTRIS_TYPEDEF_DTD_DECLARED
#define LEPTRIS_TYPEDEF_DTD_DECLARED
typedef struct LeptrisDTD LeptrisDTD;
#endif

/* Document opaque handle — pointer type, matches public types.h.
 * Do NOT redefine as `struct leptris_document` here; the public API
 * exposes `typedef struct leptris_document* LeptrisDocument`. */

#endif /* LEPTRIS_COMMON_TYPES_INTERNAL_H */
