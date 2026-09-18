/* dom/diag.c — unified error narration records (#1126). */
#include "diag.h"
#include "node.h"
#include "element.h"
#include "../leptris_internal.h"  /* leptris_strdup */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

LeptrisDiag* leptris_diag_emit(LeptrisDiag** list, int* count, int* cap,
                               LeptrisDiagKind kind, LeptrisNodeRef offender,
                               const char* fmt, ...) {
    if (!list || !count || !cap) return NULL;
    if (*count == *cap) {
        int nc = *cap ? *cap * 2 : 8;
        LeptrisDiag* nl = (LeptrisDiag*)realloc(
            *list, sizeof(LeptrisDiag) * (size_t)nc);
        if (!nl) return NULL;
        *list = nl;
        *cap = nc;
    }
    LeptrisDiag* d = &(*list)[*count];
    d->kind = kind;
    d->offender_name = NULL;
    d->line = 0;
    d->col_start = 0;
    d->col_end = 0;
    if (offender) {
        LeptrisSourcePosition pos;
        leptris_node_source_position(offender, &pos);
        d->line = pos.line;
        d->col_start = pos.col_start;
        d->col_end = pos.col_end;
        /* capture the name NOW (#1194): the node belongs to the
         * validated document, which the caller may free before the
         * report is read — a stored ref dangles (musl unmaps the
         * freed region and error_report segfaulted reading it). */
        const char* nm = leptris_element_name((LeptrisElement)offender);
        d->offender_name = (nm && *nm) ? leptris_strdup(nm) : NULL;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
    (*count)++;
    return d;
}

void leptris_diag_free(LeptrisDiag** list, int* count, int* cap) {
    if (!list) return;
    if (*list && count) {
        for (int i = 0; i < *count; i++) free((*list)[i].offender_name);
    }
    free(*list);
    *list = NULL;
    if (count) *count = 0;
    if (cap) *cap = 0;
}