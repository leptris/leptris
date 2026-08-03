/* dtd/validator.c — DTD validation entry points.
 *
 * The validation engine itself is not yet implemented (TODO 91).
 * Without this translation unit, the public API in taurus/dtd.h
 * (taurus_dtd_validate, taurus_dtd_error_free) would be declared but
 * undefined — calling them from a consumer would fail at link time.
 *
 * What's here now:
 * - taurus_dtd_validate: returns -1 with an error struct explaining
 *   the feature is not implemented. Callers can detect this and fall
 *   back to lenient parsing.
 * - taurus_dtd_error_free: real implementation — frees message and
 *   element_name strings if present.
 *
 * The full validator must:
 * - Match each element against its ELEMENT content model.
 * - Apply ATTLIST defaults and enforce #REQUIRED.
 * - Validate attribute types (ID, IDREF, NMTOKEN, enumerated).
 * - Resolve ENTITY references against the DTD's entity table.
 * - Report violations with element_name + line + column.
 */

#include "../../include/taurus.h"
#include "../../include/taurus/dtd.h"
#include "model.h"
#include <stdlib.h>
#include <string.h>

static char* dup_str(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1);
    if (out) {
        memcpy(out, src, len + 1);
    }
    return out;
}

int taurus_dtd_validate(TaurusDocument doc, TaurusDTD* dtd, TaurusDTDError* error) {
    (void)doc;
    (void)dtd;
    /* TODO 91: implement full DTD validation. The check requires:
     *   - Walking doc->root and matching each element against its
     *     ELEMENT content model (children must match the model grammar).
     *   - For each element with ATTLIST declarations: apply defaults,
     *     enforce #REQUIRED, validate enumerated attribute types.
     *   - For ENTITY-typed attribute values: resolve against
     *     DTD's entity table.
     *   - Report the first violation via `error`.
     *
     * Until the engine ships, signal "not implemented" via -1 so
     * callers can distinguish "validation not run" from
     * "validation ran and failed" (return 0) or "doc is valid"
     * (return 1). */
    if (error) {
        error->message = dup_str("DTD validation not implemented (see TODO 91)");
        error->element_name = NULL;
        error->line = 0;
        error->column = 0;
    }
    return -1;
}

void taurus_dtd_error_free(TaurusDTDError* error) {
    if (!error) return;
    if (error->message) {
        free(error->message);
        error->message = NULL;
    }
    if (error->element_name) {
        free(error->element_name);
        error->element_name = NULL;
    }
    error->line = 0;
    error->column = 0;
}
