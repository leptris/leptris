/* Internal cross-TU carrier decode (fold-left-009/016, #691):
 * a "\x03P" + 16-hex carrier resolves to its node pointer, valid
 * for the whole eval (ctor docs anchor on the source document).
 * NULL when the string is not a pointer carrier.
 *
 * leptris#1681: static inline — GCC LTO internalizes (hidden)
 * out-of-line definitions and the block-scope extern references in
 * functions.c / xpath_public.c / evaluator_operators.c / xquery.c
 * resolve to nothing (ppc64le/s390x/truffleruby "undefined
 * reference"); a header inline has no cross-TU symbol to drop. */
#ifndef LEPTRIS_XPATH_CARRIER_H
#define LEPTRIS_XPATH_CARRIER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void* xpath_carrier_node_of(const char* s) {
    if (!s || s[0] != '\x03' || s[1] != 'P' || strlen(s) != 18)
        return NULL;
    return (void*)(uintptr_t)strtoull(s + 2, NULL, 16);
}

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_XPATH_CARRIER_H */
