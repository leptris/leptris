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
    free(r->report);
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
        /* The string entry publishes detail; the file entry MUST
         * too — callers read leptris_last_error() for the reason
         * (isodoc-compile.rng triage died on an empty channel). */
        leptris_set_error(LEPTRIS_ERROR_PARSE_FAILED, rng->error);
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

static const char* rng_diag_kind_name(LeptrisDiagKind k) {
    switch (k) {
        case LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE:
            return "not-allowed-anywhere";
        case LEPTRIS_DIAG_NOT_ALLOWED_HERE:
            return "not-allowed-here";
        case LEPTRIS_DIAG_NOT_ALLOWED_YET:
            return "not-allowed-yet";
        case LEPTRIS_DIAG_INCOMPLETE:
            return "incomplete";
        case LEPTRIS_DIAG_MISSING_REQUIRED_ATTR:
            return "missing-required-attr";
        case LEPTRIS_DIAG_ATTR_NOT_ALLOWED:
            return "attr-not-allowed";
        case LEPTRIS_DIAG_ATTR_VALUE_INVALID:
            return "attr-value-invalid";
        case LEPTRIS_DIAG_CHAR_CONTENT_INVALID:
            return "char-content-invalid";
        default:
            return "invalid";
    }
}

LEPTRIS_API size_t leptris_rng_error_report(
    LeptrisRelaxNG rng, const LeptrisRngErrorRecord** out) {
    struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
    if (!r || !out) return 0;
    free(r->report);
    r->report = NULL;
    r->report_count = 0;
    if (r->diag_count <= 0) {
        *out = NULL;
        return 0;
    }
    r->report = (LeptrisRngErrorRecord*)calloc(
        (size_t)r->diag_count, sizeof(LeptrisRngErrorRecord));
    if (!r->report) {
        *out = NULL;
        return 0;
    }
    LeptrisRngErrorRecord* report =
        (LeptrisRngErrorRecord*)r->report;
    for (int i = 0; i < r->diag_count; i++) {
        LeptrisRngErrorRecord* rec = &report[i];
        rec->kind = rng_diag_kind_name(r->diags[i].kind);
        rec->message = r->diags[i].message;
        rec->line = (unsigned)r->diags[i].line;
        rec->column = (unsigned)leptris_rng_error_column(rng, (size_t)i);
        rec->offender =
            r->diags[i].offender
                ? leptris_element_name(
                      (LeptrisElement)r->diags[i].offender)
                : NULL;
    }
    r->report_count = (size_t)r->diag_count;
    *out = r->report;
    return r->report_count;
}
