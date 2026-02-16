/* lib/src/common/entities.h - XML entity and character reference decoding
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef TAURUS_COMMON_ENTITIES_H
#define TAURUS_COMMON_ENTITIES_H

#include <stddef.h>
#include "string_view.h"

/* Forward declaration */
typedef struct taurus_memory_pool TaurusMemoryPool;
typedef struct TaurusDTD TaurusDTD;

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
 * Decode XML entities and character references in-place with strict mode.
 *
 * In strict mode:
 * - Unknown entities cause an error (not copied literally)
 * - Missing semicolons on character references cause an error
 *
 * @param input       Input string with entities
 * @param output      Output buffer
 * @param outlen      Size of output buffer
 * @param strict_mode 1 for strict XML 1.0, 0 for lenient (pugixml compat)
 * @return Number of characters written, or 0 on error
 */
size_t decode_entity_with_options(const char* input, char* output, size_t outlen, int strict_mode);

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
char* taurus_decode_entities(const char* input);

/**
 * Decode XML entities in strict mode.
 *
 * @param input       Input string with entities
 * @param strict_mode 1 for strict XML 1.0, 0 for lenient
 * @return Newly allocated string with entities decoded, or NULL on error.
 */
char* taurus_decode_entities_with_options(const char* input, int strict_mode);

/**
 * Decode entities in a string view, allocating a new buffer.
 *
 * @param sv    StringView with entities
 * @param pool  Memory pool for allocation (can be NULL for malloc)
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         If pool is non-NULL, memory is allocated from the pool.
 */
char* taurus_decode_entities_view(const struct taurus_string_view* sv, TaurusMemoryPool* pool);

/**
 * Decode entities in a string view with strict mode option.
 *
 * @param sv          StringView with entities
 * @param pool        Memory pool for allocation (can be NULL for malloc)
 * @param strict_mode 1 for strict XML 1.0, 0 for lenient
 * @return Newly allocated string with entities decoded, or NULL on error.
 */
char* taurus_decode_entities_view_with_options(const struct taurus_string_view* sv,
                                                TaurusMemoryPool* pool,
                                                int strict_mode);

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
char* taurus_decode_entities_with_dtd(const char* input, const TaurusDTD* dtd);

/**
 * Decode XML entities with DTD support and strict mode option.
 *
 * @param input       Input string with entities
 * @param dtd         DTD container for user-defined entities (can be NULL)
 * @param strict_mode 1 for strict XML 1.0, 0 for lenient
 * @return Newly allocated string with entities decoded, or NULL on error.
 */
char* taurus_decode_entities_with_dtd_options(const char* input,
                                               const TaurusDTD* dtd,
                                               int strict_mode);

/**
 * Decode entities in a string view with DTD support.
 *
 * @param sv    StringView with entities
 * @param dtd   DTD container for user-defined entities (can be NULL)
 * @param pool  Memory pool for allocation (can be NULL for malloc)
 * @return Newly allocated string with entities decoded, or NULL on error.
 *         If pool is non-NULL, memory is allocated from the pool.
 */
char* taurus_decode_entities_view_with_dtd(const struct taurus_string_view* sv,
                                          const TaurusDTD* dtd,
                                          TaurusMemoryPool* pool);

/**
 * Decode entities in a string view with DTD support and strict mode.
 *
 * @param sv          StringView with entities
 * @param dtd         DTD container for user-defined entities (can be NULL)
 * @param pool        Memory pool for allocation (can be NULL for malloc)
 * @param strict_mode 1 for strict XML 1.0, 0 for lenient
 * @return Newly allocated string with entities decoded, or NULL on error.
 */
char* taurus_decode_entities_view_with_dtd_options(const struct taurus_string_view* sv,
                                                    const TaurusDTD* dtd,
                                                    TaurusMemoryPool* pool,
                                                    int strict_mode);

#endif /* TAURUS_COMMON_ENTITIES_H */
