/* dom/diag.c — unified error narration records (#1126). */
#include "diag.h"
#include "node.h"
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
    d->offender = offender;
    d->line = 0;
    d->col_start = 0;
    d->col_end = 0;
    if (offender) {
        LeptrisSourcePosition pos;
        leptris_node_source_position(offender, &pos);
        d->line = pos.line;
        d->col_start = pos.col_start;
        d->col_end = pos.col_end;
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
    free(*list);
    *list = NULL;
    if (count) *count = 0;
    if (cap) *cap = 0;
}