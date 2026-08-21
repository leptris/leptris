#ifndef LEPTRIS_STRING_VIEW_H
#define LEPTRIS_STRING_VIEW_H

#include <stddef.h>
#include <string.h>

/* Forward declaration - defined in pool.h */
struct leptris_memory_pool;

/**
 * StringView - Length-aware string (no NULL-termination required)
 *
 * Points to substring in a buffer. Does NOT own memory.
 * Perfect for zero-copy: strings live in XML buffer.
 *
 * Guarded so re-including this header doesn't trigger C99
 * typedef-redefinition warnings.  See TODO 12.
 */
#ifndef LEPTRIS_STRING_VIEW_DEFINED
#define LEPTRIS_STRING_VIEW_DEFINED
typedef struct leptris_string_view {
    const char* data;    /* Start of string (may not be NULL-terminated!) */
    size_t length;       /* Length in bytes */
} LeptrisStringView;
#endif

/* Creation */
/* Constructors are header-inline (round 12): these are pure
 * two-field initializers on the hottest paths in the library —
 * leptris_parse alone calls sv_from_ptr twice PER ATTRIBUTE, and as
 * out-of-line definitions in another TU they cost a function call
 * each (~4ns/attr, the bulk of the measured gap vs pugixml). */
static inline LeptrisStringView leptris_sv_from_ptr(const char* data,
                                                  size_t length) {
    LeptrisStringView sv = { data, length };
    return sv;
}

static inline LeptrisStringView leptris_sv_from_cstr(const char* str) {
    LeptrisStringView sv = { str ? str : NULL, str ? strlen(str) : 0 };
    return sv;
}

static inline LeptrisStringView leptris_sv_empty(void) {
    LeptrisStringView sv = { NULL, 0 };
    return sv;
}

/* Query */
int leptris_sv_is_empty(const LeptrisStringView* sv);
size_t leptris_sv_length(const LeptrisStringView* sv);

/* Comparison */
int leptris_sv_equals(const LeptrisStringView* a, const LeptrisStringView* b);
int leptris_sv_equals_cstr(const LeptrisStringView* sv, const char* str);

/* Hash (for hash table operations) */
size_t leptris_sv_hash(const LeptrisStringView* sv);

/* Literal macro (compile-time StringView from string literal) */
#define LEPTRIS_SV_LIT(lit) ((LeptrisStringView){(lit), sizeof(lit) - 1})

/* Conversion (allocates) */
char* leptris_sv_to_cstr(const LeptrisStringView* sv);
char* leptris_sv_to_cstr_pooled(const LeptrisStringView* sv, struct leptris_memory_pool* pool);
size_t leptris_sv_to_buffer(const LeptrisStringView* sv, char* buf, size_t buf_size);

#endif /* LEPTRIS_STRING_VIEW_H */