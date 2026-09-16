/* rng/rng_public.c — public RELAX NG entries (#878 phase 1). */
#include "rng_internal.h"
#include "../leptris_internal.h"
#include <stdlib.h>

LEPTRIS_API LeptrisRelaxNG leptris_rng_parse(const char* schema,
                                              size_t len,
                                              LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!schema) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    LeptrisDocument doc = leptris_parse_string(schema, len, status);
    if (!doc) return NULL;
    struct leptris_relaxng* rng = rng_parse_document(doc);
    leptris_document_free(doc);
    if (!rng) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    if (rng->error) {
        leptris_set_error(LEPTRIS_ERROR_PARSE_FAILED, rng->error);
        leptris_rng_free((LeptrisRelaxNG)rng);
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    return (LeptrisRelaxNG)rng;
}

LEPTRIS_API void leptris_rng_free(LeptrisRelaxNG rng) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r) return;
    rng_grammar_free(r->grammar);
    free(r->error);
    leptris_diag_free(&r->diags, &r->diag_count, &r->diag_cap);
    free(r);
}

LEPTRIS_API LeptrisRelaxNG leptris_rng_parse_file(const char* path,
                                                  LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!path) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    struct leptris_relaxng* rng = rng_parse_file(path);
    if (!rng) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    if (rng->error) {
        leptris_rng_free((LeptrisRelaxNG)rng);
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    return (LeptrisRelaxNG)rng;
}

LEPTRIS_API int leptris_rng_validate(LeptrisRelaxNG rng,
                                      LeptrisDocument doc) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r || !doc) return 0;
    free(r->error);
    r->error = NULL;
    leptris_diag_free(&r->diags, &r->diag_count, &r->diag_cap);
    return rng_validate_document(r, doc);
}

LEPTRIS_API const char* leptris_rng_error(LeptrisRelaxNG rng) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    return r ? r->error : NULL;
}

LEPTRIS_API size_t leptris_rng_error_count(LeptrisRelaxNG rng) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    return r ? (size_t)r->diag_count : 0u;
}

LEPTRIS_API const char* leptris_rng_error_message(LeptrisRelaxNG rng,
                                                  size_t i) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r || i >= (size_t)r->diag_count) return NULL;
    return r->diags[i].message;
}

LEPTRIS_API int leptris_rng_error_line(LeptrisRelaxNG rng, size_t i) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r || i >= (size_t)r->diag_count) return 0;
    return r->diags[i].line;
}

LEPTRIS_API int leptris_rng_error_column(LeptrisRelaxNG rng, size_t i) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r || i >= (size_t)r->diag_count) return 0;
    return leptris_diag_kind_uses_end_col(r->diags[i].kind)
               ? r->diags[i].col_end
               : r->diags[i].col_start;
}
