/* lib/src/common/entities.c - XML entity and character reference decoding
 * Copyright (c) 2024, Ribose Inc.
 */

#include "entities.h"
#include "../dtd/model.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../memory/pool.h"

/**
 * Decode XML entities and character references in a string.
 *
 * Handles:
 * - Predefined entities: &lt; &gt; &amp; &apos; &quot;
 * - Numeric character references: &#65; &#x42;
 *
 * @param input  Input string with entities
 * @param output Output buffer (must be large enough)
 * @param outlen Size of output buffer
 * @return Number of characters written, or 0 on error
 */
size_t decode_entity(const char* input, char* output, size_t outlen) {
    if (!input || !output || outlen == 0) return 0;

    size_t in_pos = 0;
    size_t out_pos = 0;
    int has_error = 0;  /* Track if we encountered an invalid entity */

    while (input[in_pos]) {
        if (input[in_pos] == '&') {
            /* Entity or character reference */
            in_pos++; /* Skip '&' */

            if (strncmp(input + in_pos, "lt;", 3) == 0) {
                if (out_pos < outlen - 1) output[out_pos++] = '<';
                in_pos += 3;
            } else if (strncmp(input + in_pos, "gt;", 3) == 0) {
                if (out_pos < outlen - 1) output[out_pos++] = '>';
                in_pos += 3;
            } else if (strncmp(input + in_pos, "amp;", 4) == 0) {
                if (out_pos < outlen - 1) output[out_pos++] = '&';
                in_pos += 4;
            } else if (strncmp(input + in_pos, "apos;", 5) == 0) {
                if (out_pos < outlen - 1) output[out_pos++] = '\'';
                in_pos += 5;
            } else if (strncmp(input + in_pos, "quot;", 5) == 0) {
                if (out_pos < outlen - 1) output[out_pos++] = '"';
                in_pos += 5;
            } else if (input[in_pos] == '#') {
                /* Character reference: &#65; or &#x42; */
                in_pos++; /* Skip '#' */
                int is_hex = 0;
                unsigned long codepoint = 0;
                size_t digits = 0;  /* Track number of digits parsed */

                if (input[in_pos] == 'x' || input[in_pos] == 'X') {
                    is_hex = 1;
                    in_pos++; /* Skip 'x' */
                }

                /* Parse number */
                while (input[in_pos] && input[in_pos] != ';') {
                    if (is_hex) {
                        char c = tolower(input[in_pos]);
                        if (c >= '0' && c <= '9') {
                            codepoint = codepoint * 16 + (c - '0');
                            digits++;
                        } else if (c >= 'a' && c <= 'f') {
                            codepoint = codepoint * 16 + (c - 'a' + 10);
                            digits++;
                        } else {
                            has_error = 1; /* Invalid character in hex number */
                            break;
                        }
                    } else {
                        if (input[in_pos] >= '0' && input[in_pos] <= '9') {
                            codepoint = codepoint * 10 + (input[in_pos] - '0');
                            digits++;
                        } else {
                            has_error = 1; /* Invalid character in decimal number */
                            break;
                        }
                    }
                    in_pos++;
                }

                /* Check if we parsed any digits */
                if (digits == 0) {
                    has_error = 1; /* No digits found (empty entity like &#; or &#x;) */
                    break;
                }

                /* XML requires ';' at the end of character references
                 * In lenient mode, semicolon is optional (pugixml compatibility) */
                int strict_mode = taurus_get_strict_mode();
                if (input[in_pos] != ';') {
                    if (strict_mode) {
                        has_error = 1; /* Missing semicolon - error in strict mode */
                        break;
                    }
                    /* Lenient mode: accept incomplete character reference */
                } else {
                    in_pos++; /* Skip ';' */
                }

                /* Convert codepoint to UTF-8 */
                if (codepoint <= 0x7F) {
                    if (out_pos < outlen - 1) output[out_pos++] = (char)codepoint;
                } else if (codepoint <= 0x7FF) {
                    if (out_pos < outlen - 2) {
                        output[out_pos++] = (char)(0xC0 | (codepoint >> 6));
                        output[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                } else if (codepoint <= 0xFFFF) {
                    if (out_pos < outlen - 3) {
                        output[out_pos++] = (char)(0xE0 | (codepoint >> 12));
                        output[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        output[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                } else if (codepoint <= 0x10FFFF) {
                    if (out_pos < outlen - 4) {
                        output[out_pos++] = (char)(0xF0 | (codepoint >> 18));
                        output[out_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                        output[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        output[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                }
            } else {
                /* Unknown entity (e.g., &copy;, &nbsp;, etc.)
                 * In strict mode: XML well-formedness ERROR
                 * In lenient mode: Copy literal text (pugixml compatibility) */
                int strict_mode = taurus_get_strict_mode();
                if (strict_mode) {
                    /* Strict mode: Only predefined entities and character references allowed */
                    has_error = 1;
                    break;
                }
                /* Lenient mode: Copy the entity literally to output */
                /* Copy the '&' and continue reading until we find ';' or end */
                if (out_pos < outlen - 1) output[out_pos++] = '&';
                /* NOTE: in_pos already points past '&' (incremented at line 35) */

                /* Copy the entity name until ';' or non-name character */
                while (input[in_pos]) {
                    char c = input[in_pos];
                    if (c == ';') {
                        in_pos++; /* Skip ';' */
                        break;
                    }
                    /* Stop at whitespace or '<' (end of entity context) */
                    if (isspace((unsigned char)c)) break;
                    if (c == '<') break;
                    if (out_pos < outlen - 1) output[out_pos++] = c;
                    else break;  /* Buffer full */
                    in_pos++;
                }
            }
        } else {
            /* Regular character */
            if (out_pos < outlen - 1) output[out_pos++] = input[in_pos++];
            else break;  /* Buffer full, stop processing */
        }
    }

    output[out_pos] = '\0';
    return has_error ? 0 : out_pos;
}

/**
 * Decode entities in a string, allocating a new buffer.
 *
 * @param input Input string with entities
 * @return Newly allocated string with entities decoded, or NULL on error
 */
char* taurus_decode_entities(const char* input) {
    if (!input) return NULL;

    /* Calculate worst case output size (same as input) */
    size_t input_len = strlen(input);
    char* output = (char*)malloc(input_len + 1);
    if (!output) return NULL;

    size_t result = decode_entity(input, output, input_len + 1);
    if (result == 0) {
        /* Entity decoding failed (invalid entity) */
        free(output);
        return NULL;
    }
    return output;
}

/**
 * Decode entities in a string view, allocating a new buffer.
 *
 * @param sv    StringView with entities
 * @param pool  Memory pool for allocation (can be NULL for malloc)
 * @return Newly allocated string with entities decoded, or NULL on error
 */
char* taurus_decode_entities_view(const struct taurus_string_view* sv, TaurusMemoryPool* pool) {
    if (!sv || taurus_sv_is_empty(sv)) {
        if (pool) {
            char* empty = (char*)taurus_pool_alloc(pool, 1);
            if (empty) empty[0] = '\0';
            return empty;
        }
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* Create a NULL-terminated copy of the StringView data
     * The decode_entity function expects NULL-terminated input */
    char* temp_buf = (char*)malloc(sv->length + 1);
    if (!temp_buf) return NULL;
    memcpy(temp_buf, sv->data, sv->length);
    temp_buf[sv->length] = '\0';

    /* Allocate output buffer */
    char* output;
    if (pool) {
        output = (char*)taurus_pool_alloc(pool, sv->length + 1);
    } else {
        output = (char*)malloc(sv->length + 1);
    }
    if (!output) {
        free(temp_buf);
        return NULL;
    }

    /* Decode the NULL-terminated temporary buffer */
    size_t result = decode_entity(temp_buf, output, sv->length + 1);
    free(temp_buf);

    if (result == 0) {
        /* Entity decoding failed (invalid entity) */
        /* Note: output was pool-allocated, so we can't free it directly.
         * It will be freed when the pool is destroyed. */
        return NULL;
    }

    return output;
}

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
char* taurus_decode_entities_with_dtd(const char* input, const TaurusDTD* dtd) {
    if (!input) return NULL;

    /* If DTD is available, use it for full entity expansion */
    if (dtd) {
        size_t result_len = 0;
        char* result = taurus_dtd_expand_entities(dtd, input, strlen(input), &result_len);
        return result;
    }

    /* Otherwise, fall back to predefined entities only */
    return taurus_decode_entities(input);
}

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
                                          TaurusMemoryPool* pool) {
    if (!sv || taurus_sv_is_empty(sv)) {
        if (pool) {
            char* empty = (char*)taurus_pool_alloc(pool, 1);
            if (empty) empty[0] = '\0';
            return empty;
        }
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* If DTD is available, use it for full entity expansion */
    if (dtd) {
        /* Create NULL-terminated string from StringView */
        char* temp_buf = (char*)malloc(sv->length + 1);
        if (!temp_buf) return NULL;
        memcpy(temp_buf, sv->data, sv->length);
        temp_buf[sv->length] = '\0';

        /* Expand entities using DTD */
        size_t result_len = 0;
        char* result = taurus_dtd_expand_entities(dtd, temp_buf, sv->length, &result_len);
        free(temp_buf);

        if (result && pool) {
            /* Copy result to pool */
            char* pooled = (char*)taurus_pool_alloc(pool, result_len + 1);
            if (pooled) {
                memcpy(pooled, result, result_len + 1);
                free(result);
                return pooled;
            } else {
                free(result);
                return NULL;
            }
        }
        return result;
    }

    /* Otherwise, fall back to predefined entities only */
    return taurus_decode_entities_view(sv, pool);
}
