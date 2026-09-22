/* lib/src/flat/il_scan.h — the interleaved lane's record-table scan.
 *
 * Shared between the DOM build (direct_parse.c dp_il_try) and the
 * SAX records surface (sax/records.c): one scan pass produces flat
 * element/text records + a flat attribute table of (off,len) views
 * into the scratch copy. Never restored; views live as long as the
 * scratch. */
#ifndef LEPTRIS_FLAT_IL_SCAN_H
#define LEPTRIS_FLAT_IL_SCAN_H

#include <stddef.h>
#include <stdint.h>

#define LEPTRIS_IL_NONE 0xFFFFFFFFu
#define LEPTRIS_IL_MAX_DEPTH 256

/* established internal spelling */
#ifndef IL_NONE
#define IL_NONE LEPTRIS_IL_NONE
#endif

typedef struct IlRec {
    uint32_t off;            /* elem: raw name incl. prefix; text: content */
    uint32_t len;
    uint32_t parent;         /* record index, IL_NONE for the root */
    uint32_t next_sib;       /* record index, IL_NONE = last sibling */
    uint32_t line;           /* byte offset of '<' (or text start) + 1 */
    uint32_t start_tag_end;  /* byte offset after '>' (0 when !line_ok) */
    uint32_t elem_end;       /* after close '>' (self-close = start) */
    uint32_t first_attr;     /* attr array index, IL_NONE = none */
    uint32_t attr_count;
    uint8_t  kind;           /* 0 = element, 1 = text */
    uint8_t  self_closing;
    uint8_t  open_prefixed;
    uint8_t  has_open_parent;
} IlRec;

typedef struct IlAttr {
    uint32_t name_off, name_len, val_off, val_len;
    uint32_t has_ws;         /* literal \t/\n/\r — 3.3.3 normalization */
} IlAttr;

typedef struct IlCtx {
    IlRec* recs; size_t nrec, crec;
    IlAttr* attrs; size_t nattr, cattr;
    uint32_t open[LEPTRIS_IL_MAX_DEPTH];
    uint32_t last_child[LEPTRIS_IL_MAX_DEPTH];
    int depth;
} IlCtx;

/* Scan (scratch, len) into c. scratch must carry the NUL sentinel
 * pad (len + 65). Returns 0 on bail (unsupported construct:
 * comment/PI/CDATA/DOCTYPE/'&'/xmlns, malformed input, depth over
 * limit) — the caller owns freeing c's arrays on bail. */
int leptris_il_scan(char* s, size_t len, int drop_ws, IlCtx* c,
                    uint32_t* root_out, size_t* trail_off,
                    size_t* trail_len);

/* One-call entry: copies xml into a fresh NUL-padded scratch, sizes
 * and scans into c. On success *scratch_out owns the copy; free it
 * (and c->recs/c->attrs) with leptris_il_run_free. Returns 0 on
 * bail/allocation failure (c and *scratch_out left freeable-empty:
 * all NULL). */
int leptris_il_run(const char* xml, size_t len, int drop_ws,
                   IlCtx* c, char** scratch_out, uint32_t* root_out);

void leptris_il_run_free(IlCtx* c, char* scratch);

#endif /* LEPTRIS_FLAT_IL_SCAN_H */
