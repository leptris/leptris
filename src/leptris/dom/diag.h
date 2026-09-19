/* dom/diag.h — unified error narration records (#1126).
 *
 * One structured record per validation failure, shared by every
 * validator (RNG today; XSD/Schematron follow). The record carries
 * the KIND (which selects the Jing reporting convention: which
 * column, which vocabulary family), the offending node, and the
 * rendered message. The per-format verdict engines stay separate —
 * their semantics differ — but narration goes through here so the
 * vocabulary and position rules live once.
 */
#ifndef LEPTRIS_DOM_DIAG_H
#define LEPTRIS_DOM_DIAG_H

#include "../include/leptris.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LeptrisDiagKind lives in leptris/types.h (public, #1200). */

typedef struct LeptrisDiag {
    LeptrisDiagKind kind;
    char* offender_name;          /* captured at emit time (strdup'd);
                                   * NULL when the diag has no offender.
                                   * #1207: a node ref dangles once the
                                   * validated document is freed — musl
                                   * unmaps the region and the report
                                   * pass segfaulted reading it. */

    int line;
    int col_start;                /* Jing: element/attr errors */
    int col_end;                  /* Jing: incomplete/content errors */
    char message[256];
} LeptrisDiag;

/* Does this kind report at the element's END column (Jing's
 * incomplete / character-content convention)? */
static inline int leptris_diag_kind_uses_end_col(LeptrisDiagKind k) {
    return k == LEPTRIS_DIAG_INCOMPLETE ||
           k == LEPTRIS_DIAG_CHAR_CONTENT_INVALID;
}

/* Append one record. Position comes from the parser-recorded
 * source offsets (#1124); the column is chosen by kind. The
 * message is vsnprintf'd from fmt. Returns the appended record or
 * NULL when allocation failed. */
LeptrisDiag* leptris_diag_emit(LeptrisDiag** list, int* count, int* cap,
                               LeptrisDiagKind kind, LeptrisNodeRef offender,
                               const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;

/* Release a record list (the messages are inline, so this is one
 * free + reset). */
void leptris_diag_free(LeptrisDiag** list, int* count, int* cap);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DOM_DIAG_H */