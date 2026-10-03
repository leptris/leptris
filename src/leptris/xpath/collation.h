#ifndef LEPTRIS_XPATH_COLLATION_H
#define LEPTRIS_XPATH_COLLATION_H

/* Collation support: codepoint / html-ascii-case-insensitive (always
 * available) and the Unicode Collation Algorithm over the vendored
 * DUCET table (built with LEPTRIS_ENABLE_DUCET, which requires
 * LEPTRIS_ENABLE_UTF8PROC for NFD).
 *
 * UCA sort keys follow the F&O 3.x defaults: normalization on,
 * strength tertiary, alternate non-ignorable. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEPTRIS_COLL_CODEPOINT = 0,
    LEPTRIS_COLL_ASCII_CI = 1,
    LEPTRIS_COLL_UCA = 2,
} leptris_coll_kind;

enum {
    LEPTRIS_COLL_STRENGTH_PRIMARY = 1,
    LEPTRIS_COLL_STRENGTH_SECONDARY = 2,
    LEPTRIS_COLL_STRENGTH_TERTIARY = 3,
    LEPTRIS_COLL_STRENGTH_QUATERNARY = 4,
    LEPTRIS_COLL_STRENGTH_IDENTICAL = 5,
};

enum {
    LEPTRIS_COLL_ALTERNATE_NON_IGNORABLE = 0,
    LEPTRIS_COLL_ALTERNATE_SHIFTED = 1,
};

typedef struct {
    uint8_t kind;
    uint8_t strength;  /* 1..5; default tertiary */
    uint8_t alternate; /* non-ignorable / shifted */
} leptris_collation;

/* Parse a collation URI. Returns 0 on success, -1 when the URI or a
 * parameter value is not supported (out is untouched). */
int leptris_collation_from_uri(const char* uri, leptris_collation* out);

/* -1 / 0 / 1 comparison under the descriptor. */
int leptris_collation_compare(const char* a, size_t alen,
                              const char* b, size_t blen,
                              const leptris_collation* c);

/* UCA sort key into a malloc'd buffer (caller frees). Returns 0 on
 * success, -1 on allocation failure. Non-UCA descriptors are
 * compared directly by leptris_collation_compare. */
int leptris_collation_sortkey(const char* s, size_t len,
                              const leptris_collation* c,
                              unsigned char** out, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif
