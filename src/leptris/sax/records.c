/* lib/src/sax/records.c — bulk SAX record table (#1298).
 *
 * One FFI crossing drains the whole document: leptris_il_run scans
 * the input into flat (off,len) record + attribute tables, mapped
 * here to the public structs. Views index the parser-owned scratch
 * copy (NUL-padded), so hosts walk plain C structs with zero
 * per-event marshaling.
 *
 * Coverage = the interleaved lane's scannable subset (elements,
 * text, attributes; no comments/PIs/CDATA/DOCTYPE/'&'/xmlns). A scan
 * bail returns LEPTRIS_ERROR_NOT_SUPPORTED with nothing allocated —
 * hosts fall back to the pull API for those documents.
 */

#include "../../include/leptris/sax/sax.h"
#include "../leptris_internal.h"
#include "../flat/il_scan.h"
#include <stdlib.h>
#include <string.h>

struct LeptrisSaxRecords {
    char* buf;                 /* scratch copy (NUL-padded) */
    size_t buf_len;
    LeptrisSaxRecord* recs;    /* public mapping */
    size_t nrec;
    LeptrisSaxAttr* attrs;     /* public mapping */
    size_t nattr;
};

LeptrisStatus leptris_sax_records_parse(const char* xml, size_t len,
                                        unsigned flags,
                                        LeptrisSaxRecords** out) {
    if (out) *out = NULL;
    if (!xml || !out) return LEPTRIS_ERROR_NULL_ARG;
    if (flags != 0) return LEPTRIS_ERROR_INVALID_ARG;

    IlCtx c;
    char* scratch = NULL;
    uint32_t root;
    if (!leptris_il_run(xml, len, 0, &c, &scratch, &root)) {
        /* Distinguish too-large from bail: il_run rejects len >= 2GiB
         * and allocation failure identically; both are fine as
         * NOT_SUPPORTED for the records surface. */
        return LEPTRIS_ERROR_NOT_SUPPORTED;
    }

    LeptrisSaxRecords* r = (LeptrisSaxRecords*)malloc(sizeof(*r));
    if (!r) {
        leptris_il_run_free(&c, scratch);
        return LEPTRIS_ERROR_MEMORY;
    }
    r->buf = scratch;
    r->buf_len = len;
    r->nrec = c.nrec;
    r->nattr = c.nattr;
    r->recs = (LeptrisSaxRecord*)malloc(
        (c.nrec ? c.nrec : 1) * sizeof(LeptrisSaxRecord));
    r->attrs = (LeptrisSaxAttr*)malloc(
        (c.nattr ? c.nattr : 1) * sizeof(LeptrisSaxAttr));
    if (!r->recs || !r->attrs) {
        leptris_sax_records_free(r);
        return LEPTRIS_ERROR_MEMORY;
    }

    /* Terminate every view in the scratch: the scan leaves raw
     * delimiter bytes ('<' after text, '=' after attr names, quotes
     * after values); in records mode the buffer is dead after the
     * scan, so the NUL writes are free. */
    for (size_t i = 0; i < c.nrec; i++) {
        scratch[c.recs[i].off + c.recs[i].len] = '\0';
    }
    for (size_t i = 0; i < c.nattr; i++) {
        scratch[c.attrs[i].name_off + c.attrs[i].name_len] = '\0';
        scratch[c.attrs[i].val_off + c.attrs[i].val_len] = '\0';
    }

    for (size_t i = 0; i < c.nrec; i++) {
        const IlRec* s = &c.recs[i];
        LeptrisSaxRecord* d = &r->recs[i];
        d->kind = s->kind;
        d->parent = s->parent;
        d->next_sib = s->next_sib;
        d->off = s->off;
        d->len = s->len;
        d->line = s->line;
        d->start_tag_end = s->start_tag_end;
        d->elem_end = s->elem_end;
        d->attr_first = s->first_attr;
        d->attr_count = s->attr_count;
        d->self_closing = s->self_closing;
    }
    for (size_t i = 0; i < c.nattr; i++) {
        const IlAttr* s = &c.attrs[i];
        LeptrisSaxAttr* d = &r->attrs[i];
        d->name_off = s->name_off;
        d->name_len = s->name_len;
        d->value_off = s->val_off;
        d->value_len = s->val_len;
        d->value_has_ws = s->has_ws ? 1 : 0;
    }

    /* The internal tables are mapped into the public arrays;
     * free them, keep the scratch (r->buf) for the views. */
    leptris_il_run_free(&c, NULL);
    *out = r;
    return LEPTRIS_OK;
}

size_t leptris_sax_records_count(const LeptrisSaxRecords* recs) {
    return recs ? recs->nrec : 0;
}

const LeptrisSaxRecord* leptris_sax_records_data(
    const LeptrisSaxRecords* recs) {
    return recs ? recs->recs : NULL;
}

const LeptrisSaxAttr* leptris_sax_records_attrs(
    const LeptrisSaxRecords* recs, size_t* out_attr_count) {
    if (out_attr_count)
        *out_attr_count = recs ? recs->nattr : 0;
    return recs ? recs->attrs : NULL;
}

const char* leptris_sax_records_buffer(const LeptrisSaxRecords* recs) {
    return recs ? recs->buf : NULL;
}

void leptris_sax_records_free(LeptrisSaxRecords* recs) {
    if (!recs) return;
    free(recs->recs);
    free(recs->attrs);
    free(recs->buf);
    free(recs);
}
