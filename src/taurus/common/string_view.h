#ifndef TAURUS_STRING_VIEW_H
#define TAURUS_STRING_VIEW_H

#include <stddef.h>
#include <string.h>

/* Forward declaration - defined in pool.h */
struct taurus_memory_pool;

/**
 * StringView - Length-aware string (no NULL-termination required)
 *
 * Points to substring in a buffer. Does NOT own memory.
 * Perfect for zero-copy: strings live in XML buffer.
 *
 * Guarded so re-including this header doesn't trigger C99
 * typedef-redefinition warnings.  See TODO 12.
 */
#ifndef TAURUS_STRING_VIEW_DEFINED
#define TAURUS_STRING_VIEW_DEFINED
typedef struct taurus_string_view {
    const char* data;    /* Start of string (may not be NULL-terminated!) */
    size_t length;       /* Length in bytes */
} TaurusStringView;
#endif

/* Creation */
/* Constructors are header-inline (round 12): these are pure
 * two-field initializers on the hottest paths in the library —
 * taurus_parse alone calls sv_from_ptr twice PER ATTRIBUTE, and as
 * out-of-line definitions in another TU they cost a function call
 * each (~4ns/attr, the bulk of the measured gap vs pugixml). */
static inline TaurusStringView taurus_sv_from_ptr(const char* data,
                                                  size_t length) {
    TaurusStringView sv = { data, length };
    return sv;
}

static inline TaurusStringView taurus_sv_from_cstr(const char* str) {
    TaurusStringView sv = { str ? str : NULL, str ? strlen(str) : 0 };
    return sv;
}

static inline TaurusStringView taurus_sv_empty(void) {
    TaurusStringView sv = { NULL, 0 };
    return sv;
}

/* Query */
int taurus_sv_is_empty(const TaurusStringView* sv);
size_t taurus_sv_length(const TaurusStringView* sv);

/* Comparison */
int taurus_sv_equals(const TaurusStringView* a, const TaurusStringView* b);
int taurus_sv_equals_cstr(const TaurusStringView* sv, const char* str);

/* Hash (for hash table operations) */
size_t taurus_sv_hash(const TaurusStringView* sv);

/* Literal macro (compile-time StringView from string literal) */
#define TAURUS_SV_LIT(lit) ((TaurusStringView){(lit), sizeof(lit) - 1})

/* Conversion (allocates) */
char* taurus_sv_to_cstr(const TaurusStringView* sv);
char* taurus_sv_to_cstr_pooled(const TaurusStringView* sv, struct taurus_memory_pool* pool);
size_t taurus_sv_to_buffer(const TaurusStringView* sv, char* buf, size_t buf_size);

#endif /* TAURUS_STRING_VIEW_H */