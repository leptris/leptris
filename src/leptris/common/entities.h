/* lib/src/common/entities.h - XML entity and character reference decoding
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef LEPTRIS_COMMON_ENTITIES_H
#define LEPTRIS_COMMON_ENTITIES_H

#include <stddef.h>
#include "string_view.h"
#include "types_internal.h"   /* Single source for LeptrisMemoryPool / LeptrisDTD */

/**
 * Decode XML entities and character references in-place.
 *
 * Handles:
 * - Predefined entities: &lt; &gt; &amp; &apos; &quot;
 * - Numeric character references: &#65; &#x42;
 *
 * @param input  Input string with entities
 * @param output Output buffer
 * @param outlen Size of output buffer
 * @return Number of characters written, or 0 on error
 */
size_t decode_entity(const char* input, char* output, size_t outlen);

/**
 * Decode XML entities and character references in a string.
 *
 * Handles:
 * - Predefined entities: &lt; &gt; &amp; &apos; &quot;
 * - Numeric character references: &#65; &#x42;
 *
 * @param input Input string with entities
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         Caller must free() the returned string.
 */
char* leptris_decode_entities(const char* input);

/**
 * Decode entities in a string view, allocating a new buffer.
 *
 * @param sv    StringView with entities
 * @param pool  Memory pool for allocation (can be NULL for malloc)
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         If pool is non-NULL, memory is allocated from the pool.
 */
char* leptris_decode_entities_view(const struct leptris_string_view* sv, LeptrisMemoryPool* pool);

/* ============================================================================
 * DTD-Aware Entity Decoding - Supports user-defined entities from DTD
 * ============================================================================ */

/**
 * Decode XML entities with DTD support.
 *
 * This function expands both predefined entities and user-defined entities
 * from the DTD. If dtd is NULL, only predefined entities are expanded.
 *
 * @param input Input string with entities
 * @param dtd   DTD container for user-defined entities (can be NULL)
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         Caller must free() the returned string.
 */
char* leptris_decode_entities_with_dtd(const char* input, const LeptrisDTD* dtd);

/**
 * Decode entities in a string view with DTD support.
 *
 * @param sv    StringView with entities
 * @param dtd   DTD container for user-defined entities (can be NULL)
 * @param pool  Memory pool for allocation (can be NULL for malloc)
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         If pool is non-NULL, memory is allocated from the pool.
 */
char* leptris_decode_entities_view_with_dtd(const struct leptris_string_view* sv,
                                          const LeptrisDTD* dtd,
                                          LeptrisMemoryPool* pool);

#endif /* LEPTRIS_COMMON_ENTITIES_H */
