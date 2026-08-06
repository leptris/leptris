/* lib/src/sax/streaming.c
 *
 * TODO 116 — Streaming SAX state machine.
 *
 * The legacy recursive-descent parser (parser.c) cannot be resumed
 * mid-token: when the recursive call stack is unwound, the parse
 * position lives in call-stack-allocated locals.  We therefore keep
 * taurus_sax_parse() (one-shot) using the recursive path -- that's
 * fast and simple -- and offer the streaming path as an opt-in via
 * taurus_sax_parser_set_streaming(1).
 *
 * Carry-over model
 * ----------------
 * A state that can be interrupted mid-token (name scan, attribute
 * value, comment, CDATA, PI, closing-tag name) uses `carry` to hold
 * the partial bytes.  On the next feed(), the state is re-entered
 * with carry_len > 0; the state treats carry as the prefix of the
 * token and continues scanning into the new input.
 *
 *   carry = [partial bytes from last feed]
 *   new input = [p->pos, p->end)
 *
 * The state scans carry first (it's already in memory), then new
 * input.  When the token completes:
 *   - The full token bytes are copied to the scratch arena
 *     (carry bytes + new-input bytes consumed).
 *   - p->pos advances past the consumed new-input bytes.
 *   - carry is reset.
 *
 * Memory cost: max single-token length.  For typical XML this is the
 * longest attribute value or comment, both bounded by document
 * content (not document size -- a 1 GB stream of <p>text</p> has
 * max token ~4 bytes).
 *
 * State semantics
 * ---------------
 * Each handler returns one of:
 *   SAX_STEP_OK        — progress; dispatcher calls the same state again
 *   SAX_STEP_DONE      — state complete; dispatcher advances state
 *   SAX_STEP_NEED_MORE — ran out of input mid-token; suspend until next feed
 *   SAX_STEP_ERR       — parse error (already reported via handler)
 */

#include "sax_internal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#define SAX_STEP_OK         0
#define SAX_STEP_DONE       1
#define SAX_STEP_NEED_MORE  2
#define SAX_STEP_ERR       -1

/* ============================================================================
 * Chartype table (mirrors parser.c; kept private to avoid coupling)
 * ============================================================================ */

#define SAX_CT_SPACE       1
#define SAX_CT_NAME_START  2
#define SAX_CT_NAME_CHAR   4
#define SAX_CT_QUOTE       16

static const unsigned char sxs_chartype[256] = {
    /*       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    /* 0 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
    /* 1 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2 */  1, 0,16, 0, 0, 0, 0,16, 0, 0, 0, 0, 0, 4, 4, 0,
    /* 3 */ 12,12,12,12,12,12,12,12,12,12, 4, 0, 0, 0, 0, 0,
    /* 4 */  0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 5 */  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 6,
    /* 6 */  0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 7 */  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 0,
    /* 8 */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* 9 */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* A */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* B */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* C */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* D */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* E */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* F */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

static inline int sxs_is_ws(char c)        { return (sxs_chartype[(unsigned char)c] & SAX_CT_SPACE)      != 0; }
static inline int sxs_is_name_start(char c){ return (sxs_chartype[(unsigned char)c] & SAX_CT_NAME_START) != 0; }
static inline int sxs_is_name_char(char c) { return (sxs_chartype[(unsigned char)c] & SAX_CT_NAME_CHAR)  != 0; }
static inline int sxs_is_quote(char c)     { return (sxs_chartype[(unsigned char)c] & SAX_CT_QUOTE)      != 0; }

/* Forward decl — defined below; many helpers call it on OOM. */
static void sxs_set_error(TaurusSAXParser* p, const char* msg);

static void sxs_set_error(TaurusSAXParser* p, const char* msg) {
    snprintf(p->error_message, sizeof(p->error_message), "%s", msg);
    p->has_error = 1;
    if (p->handler && p->handler->error) {
        p->handler->error(p->user_data, msg, p->line, p->column);
    }
    p->state = SAX_ST_ERROR;
}

/* ============================================================================
 * Carry buffer
 * ============================================================================ */

static int sxs_carry_reserve(TaurusSAXParser* p, size_t extra) {
    if (p->carry_len + extra <= p->carry_cap) return 0;
    size_t new_cap = p->carry_cap ? p->carry_cap : 64;
    while (new_cap < p->carry_len + extra) new_cap *= 2;
    char* grown = (char*)realloc(p->carry, new_cap);
    if (!grown) return -1;
    p->carry = grown;
    p->carry_cap = new_cap;
    return 0;
}

static int sxs_carry_putc(TaurusSAXParser* p, char c) {
    if (sxs_carry_reserve(p, 1) < 0) return -1;
    p->carry[p->carry_len++] = c;
    return 0;
}

static int sxs_carry_append(TaurusSAXParser* p, const char* src, size_t n) {
    if (n == 0) return 0;
    if (sxs_carry_reserve(p, n) < 0) return -1;
    memcpy(p->carry + p->carry_len, src, n);
    p->carry_len += n;
    return 0;
}

static void sxs_carry_reset(TaurusSAXParser* p) { p->carry_len = 0; }

/* ============================================================================
 * Scratch arena (persisted across feed() for element names + attrs)
 * ============================================================================ */

static const char* sxs_scratch_append(TaurusSAXParser* p,
                                      const char* src, size_t n) {
    if (p->scratch_len + n + 1 > p->scratch_cap) {
        size_t new_cap = p->scratch_cap ? p->scratch_cap : 256;
        while (new_cap < p->scratch_len + n + 1) new_cap *= 2;
        char* grown = (char*)realloc(p->scratch, new_cap);
        if (!grown) return NULL;
        p->scratch = grown;
        p->scratch_cap = new_cap;
    }
    if (n) memcpy(p->scratch + p->scratch_len, src, n);
    p->scratch[p->scratch_len + n] = '\0';
    const char* result = p->scratch + p->scratch_len;
    p->scratch_len += n + 1;
    return result;
}

/* Materialize the current carry as a NUL-terminated scratch string.
 * Returns NULL on OOM.  Does NOT reset carry (caller does). */
static const char* sxs_scratch_from_carry(TaurusSAXParser* p) {
    return sxs_scratch_append(p, p->carry, p->carry_len);
}

/* ============================================================================
 * Element stack
 * ============================================================================ */

static int sxs_elem_push(TaurusSAXParser* p, const char* name, size_t name_len) {
    if (p->elem_depth >= TAURUS_SAX_MAX_DEPTH) {
        sxs_set_error(p, "Element nesting too deep");
        return -1;
    }
    SaxElementFrame* f = &p->elem_stack[p->elem_depth];
    /* name already lives in scratch (we materialized it there); just
     * keep the pointer. */
    f->name = (char*)name;
    f->name_len = name_len;
    f->attrs = NULL;
    f->attr_count = 0;
    f->attr_cap = 0;
    f->self_closing = 0;
    p->elem_depth++;
    return 0;
}

static void sxs_elem_pop(TaurusSAXParser* p) {
    if (p->elem_depth == 0) return;
    p->elem_depth--;
    SaxElementFrame* f = &p->elem_stack[p->elem_depth];
    if (f->attrs) free(f->attrs);
    f->attrs = NULL;
    f->attr_count = 0;
    f->attr_cap = 0;
}

static SaxElementFrame* sxs_elem_top(TaurusSAXParser* p) {
    return p->elem_depth > 0 ? &p->elem_stack[p->elem_depth - 1] : NULL;
}

/* Append one name/value pair to the top frame's attr list.  Pointers
 * are scratch-owned. */
static int sxs_elem_add_attr(TaurusSAXParser* p, const char* name, const char* value) {
    SaxElementFrame* f = sxs_elem_top(p);
    if (!f) return -1;
    if (f->attr_count * 2 + 2 >= f->attr_cap) {
        size_t new_cap = f->attr_cap == 0 ? 8 : f->attr_cap * 2;
        char** grown = (char**)realloc(f->attrs, (new_cap + 1) * sizeof(char*));
        if (!grown) return -1;
        f->attrs = grown;
        f->attr_cap = new_cap;
    }
    f->attrs[f->attr_count * 2]     = (char*)name;
    f->attrs[f->attr_count * 2 + 1] = (char*)value;
    f->attr_count++;
    f->attrs[f->attr_count * 2] = NULL;  /* NULL terminator */
    return 0;
}/* ============================================================================
 * Event emitters
 * ============================================================================ */

static void sxs_emit_start_document(TaurusSAXParser* p) {
    if (p->start_emitted) return;
    if (p->handler && p->handler->start_document) {
        p->handler->start_document(p->user_data);
    }
    p->start_emitted = 1;
}

static void sxs_emit_end_document(TaurusSAXParser* p) {
    if (p->end_emitted) return;
    if (p->handler && p->handler->end_document) {
        p->handler->end_document(p->user_data);
    }
    p->end_emitted = 1;
}

static void sxs_emit_start_element(TaurusSAXParser* p, SaxElementFrame* f) {
    if (!p->handler || !p->handler->start_element) return;
    const char** attrs = f->attrs ? (const char**)f->attrs : NULL;
    static const char* empty[1] = {NULL};
    p->handler->start_element(p->user_data, f->name, attrs ? attrs : empty);
}

static void sxs_emit_end_element(TaurusSAXParser* p, SaxElementFrame* f) {
    if (p->handler && p->handler->end_element) {
        p->handler->end_element(p->user_data, f->name);
    }
}

static void sxs_emit_characters(TaurusSAXParser* p, const char* text, size_t len) {
    if (p->handler && p->handler->characters && len > 0) {
        p->handler->characters(p->user_data, text, len);
    }
}

static void sxs_emit_prefix_mappings(TaurusSAXParser* p, SaxElementFrame* f, int is_start) {
    if (!f->attrs || !p->handler) return;
    for (size_t i = 0; i < f->attr_count * 2; i += 2) {
        const char* an = f->attrs[i];
        const char* av = f->attrs[i + 1];
        if (strcmp(an, "xmlns") == 0) {
            if (is_start && p->handler->start_prefix_mapping) {
                p->handler->start_prefix_mapping(p->user_data, "", av);
            } else if (!is_start && p->handler->end_prefix_mapping) {
                p->handler->end_prefix_mapping(p->user_data, "");
            }
        } else if (strncmp(an, "xmlns:", 6) == 0) {
            if (is_start && p->handler->start_prefix_mapping) {
                p->handler->start_prefix_mapping(p->user_data, an + 6, av);
            } else if (!is_start && p->handler->end_prefix_mapping) {
                p->handler->end_prefix_mapping(p->user_data, an + 6);
            }
        }
    }
}

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static int sxs_dispatch(TaurusSAXParser* p, int is_final);

/* ============================================================================
 * State: SAX_ST_TOPLEVEL
 * Skip leading whitespace, optional <?xml ?> and <!DOCTYPE>, then expect '<'.
 * ============================================================================ */

static int sxs_step_top(TaurusSAXParser* p, int is_final) {
    /* Whitespace run. */
    while (p->pos < p->end && sxs_is_ws(*p->pos)) p->pos++;

    if (p->pos >= p->end) {
        return is_final ? SAX_STEP_DONE : SAX_STEP_NEED_MORE;
    }

    if (*p->pos != '<') {
        sxs_set_error(p, "Expected '<' at top level");
        return SAX_STEP_ERR;
    }

    /* Peek at what follows '<' to dispatch.  Need at least 2 bytes to
     * distinguish element ('<x') from PI ('<?'), comment/DOCTYPE/CDATA
     * ('<!').  For '<!' we need 4 bytes for '<!--' or 9 for '<!DOCTYPE'
     * and '<![CDATA['.  Dispatch progressively so a chunk_size=1 stream
     * doesn't stall at TOPLEVEL waiting for 9 bytes that may never be
     * needed (e.g. for a plain '<r>' root). */
    if (p->end - p->pos < 2 && !is_final) {
        if (sxs_carry_append(p, p->pos, (size_t)(p->end - p->pos)) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos = p->end;
        return SAX_STEP_NEED_MORE;
    }

    const char* q = p->pos;
    size_t avail = (size_t)(p->end - q);
    char kind = (avail >= 2) ? q[1] : '\0';

    /* Element: '<' + name-start. */
    if (kind != '?' && kind != '!') {
        if (avail >= 2 && sxs_is_name_start(kind)) {
            p->pos++;  /* consume '<' */
            p->state = SAX_ST_ELEM_OPEN_NAME;
            sxs_carry_reset(p);
            return SAX_STEP_OK;
        }
        if (avail >= 2) {
            sxs_set_error(p, "Unexpected character after '<'");
            return SAX_STEP_ERR;
        }
        /* avail < 2 only reachable when is_final; fall through to best-effort below. */
    }

    /* PI: '<?' (may also be XML decl if followed by 'xml' + ws/'?'). */
    if (kind == '?') {
        /* Need at least 2 bytes; defer to detailed handler below if we have them. */
    }

    /* Comment / DOCTYPE / CDATA: '<!'. */
    if (kind == '!') {
        /* Need 4 bytes to distinguish '<!--' from '<!D...'/'<![...'. */
        if (avail < 4 && !is_final) {
            if (sxs_carry_append(p, q, avail) < 0) {
                sxs_set_error(p, "out of memory");
                return SAX_STEP_ERR;
            }
            p->pos = p->end;
            return SAX_STEP_NEED_MORE;
        }
        if (avail >= 4 && q[2] == '-' && q[3] == '-') {
            /* Comment.  Detailed handler below. */
        } else {
            /* DOCTYPE or CDATA: need 9 bytes. */
            if (avail < 9 && !is_final) {
                if (sxs_carry_append(p, q, avail) < 0) {
                    sxs_set_error(p, "out of memory");
                    return SAX_STEP_ERR;
                }
                p->pos = p->end;
                return SAX_STEP_NEED_MORE;
            }
        }
    }

    /* <?xml ... ?> — XML declaration.  Only valid at very start. */
    if (avail >= 5 && q[0] == '<' && q[1] == '?' &&
        q[2] == 'x' && q[3] == 'm' && q[4] == 'l' &&
        (avail < 6 || q[5] == ' ' || q[5] == '\t' || q[5] == '\n' || q[5] == '\r' || q[5] == '?')) {
        /* Skip to "?>". */
        const char* found = NULL;
        for (const char* s = q + 5; s + 1 < p->end; s++) {
            if (s[0] == '?' && s[1] == '>') { found = s; break; }
        }
        if (!found) {
            if (is_final) {
                sxs_set_error(p, "Unterminated XML declaration");
                return SAX_STEP_ERR;
            }
            /* Buffer remainder and ask for more. */
            if (sxs_carry_append(p, q, (size_t)(p->end - q)) < 0) {
                sxs_set_error(p, "out of memory");
                return SAX_STEP_ERR;
            }
            p->pos = p->end;
            return SAX_STEP_NEED_MORE;
        }
        p->pos = found + 2;
        sxs_carry_reset(p);
        return SAX_STEP_OK;  /* Stay in TOPLEVEL; may have more decls/misc. */
    }

    /* <!DOCTYPE ... > */
    if (avail >= 9 && memcmp(q, "<!DOCTYPE", 9) == 0) {
        const char* found = NULL;
        /* Naive scan: DOCTYPE ends at first '>'.  (Internal subset
         * with nested '>' inside quotes is not handled here; that's
         * Phase B territory.) */
        for (const char* s = q + 9; s < p->end; s++) {
            if (*s == '>') { found = s; break; }
        }
        if (!found) {
            if (is_final) {
                sxs_set_error(p, "Unterminated DOCTYPE");
                return SAX_STEP_ERR;
            }
            if (sxs_carry_append(p, q, (size_t)(p->end - q)) < 0) {
                sxs_set_error(p, "out of memory");
                return SAX_STEP_ERR;
            }
            p->pos = p->end;
            return SAX_STEP_NEED_MORE;
        }
        p->pos = found + 1;
        sxs_carry_reset(p);
        return SAX_STEP_OK;
    }

    /* <!-- ... --> (pre-root comment). */
    if (avail >= 4 && memcmp(q, "<!--", 4) == 0) {
        const char* start = q + 4;
        const char* found = NULL;
        for (const char* s = start; s + 2 < p->end; s++) {
            if (s[0] == '-' && s[1] == '-' && s[2] == '>') { found = s; break; }
        }
        if (!found) {
            if (is_final) {
                sxs_set_error(p, "Unterminated comment");
                return SAX_STEP_ERR;
            }
            /* Save what we have so far (the comment body, not the opener)
             * and resume.  We'll need to remember we're mid-comment;
             * since TOPLEVEL doesn't track that, transition to a
             * dedicated state. */
            /* For Phase A simplicity, buffer everything and retry. */
            if (sxs_carry_append(p, q, (size_t)(p->end - q)) < 0) {
                sxs_set_error(p, "out of memory");
                return SAX_STEP_ERR;
            }
            p->pos = p->end;
            return SAX_STEP_NEED_MORE;
        }
        size_t len = (size_t)(found - start);
        if (p->handler && p->handler->comment && len > 0) {
            const char* c = sxs_scratch_append(p, start, len);
            if (c) p->handler->comment(p->user_data, c);
        }
        p->pos = found + 3;
        sxs_carry_reset(p);
        return SAX_STEP_OK;
    }

    /* <? ... ?> (pre-root PI). */
    if (avail >= 2 && q[0] == '<' && q[1] == '?') {
        /* Find "?>" */
        const char* found = NULL;
        for (const char* s = q + 2; s + 1 < p->end; s++) {
            if (s[0] == '?' && s[1] == '>') { found = s; break; }
        }
        if (!found) {
            if (is_final) {
                sxs_set_error(p, "Unterminated PI");
                return SAX_STEP_ERR;
            }
            if (sxs_carry_append(p, q, (size_t)(p->end - q)) < 0) {
                sxs_set_error(p, "out of memory");
                return SAX_STEP_ERR;
            }
            p->pos = p->end;
            return SAX_STEP_NEED_MORE;
        }
        /* Parse target + data inline. */
        const char* pi_start = q + 2;
        const char* pi_end = found;
        const char* ts = pi_start;
        const char* te = ts;
        while (te < pi_end && !sxs_is_ws(*te)) te++;
        const char* target = sxs_scratch_append(p, ts, (size_t)(te - ts));
        const char* ds = te;
        while (ds < pi_end && sxs_is_ws(*ds)) ds++;
        const char* data = sxs_scratch_append(p, ds, (size_t)(pi_end - ds));
        if (p->handler && p->handler->processing_instruction) {
            p->handler->processing_instruction(p->user_data, target, data);
        }
        p->pos = found + 2;
        sxs_carry_reset(p);
        return SAX_STEP_OK;
    }

    /* Otherwise: it's an element.  Fall through to ELEM_OPEN_NAME. */
    p->pos++;  /* consume '<' */
    p->state = SAX_ST_ELEM_OPEN_NAME;
    sxs_carry_reset(p);
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ELEM_OPEN_NAME
 * Scan the element name; push frame onto elem_stack.
 * ============================================================================ */

static int sxs_step_elem_open_name(TaurusSAXParser* p, int is_final) {
    /* First byte must be a name-start char. */
    if (p->carry_len == 0) {
        if (p->pos >= p->end) {
            return is_final ? (sxs_set_error(p, "Expected element name"), SAX_STEP_ERR)
                            : SAX_STEP_NEED_MORE;
        }
        if (!sxs_is_name_start(*p->pos)) {
            sxs_set_error(p, "Expected element name");
            return SAX_STEP_ERR;
        }
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }

    /* Scan name chars into carry. */
    while (p->pos < p->end && sxs_is_name_char(*p->pos)) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }

    if (p->pos >= p->end) {
        /* End of input mid-name.  If is_final, the name is incomplete
         * but we accept it (best effort).  Otherwise suspend. */
        if (!is_final) return SAX_STEP_NEED_MORE;
    }

    /* Materialize name and push frame. */
    const char* name = sxs_scratch_from_carry(p);
    if (!name) {
        sxs_set_error(p, "out of memory");
        return SAX_STEP_ERR;
    }
    if (sxs_elem_push(p, name, p->carry_len) < 0) {
        return SAX_STEP_ERR;
    }
    sxs_carry_reset(p);
    p->state = SAX_ST_ATTR_LIST;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ATTR_LIST
 * Inside opening tag.  Dispatch: whitespace, '/>', '>', or attr name.
 * ============================================================================ */

static int sxs_step_attr_list(TaurusSAXParser* p, int is_final) {
    /* Skip whitespace. */
    while (p->pos < p->end && sxs_is_ws(*p->pos)) p->pos++;
    if (p->pos >= p->end) {
        return is_final ? (sxs_set_error(p, "Unterminated opening tag"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }

    char c = *p->pos;

    /* '>' — end of opening tag. */
    if (c == '>') {
        p->pos++;
        SaxElementFrame* f = sxs_elem_top(p);
        if (f) {
            sxs_emit_prefix_mappings(p, f, 1);
            sxs_emit_start_element(p, f);
        }
        p->state = SAX_ST_ELEM_CONTENT;
        return SAX_STEP_OK;
    }

    /* '/>' — self-closing. */
    if (c == '/' && p->pos + 1 < p->end && p->pos[1] == '>') {
        p->pos += 2;
        SaxElementFrame* f = sxs_elem_top(p);
        if (f) {
            sxs_emit_prefix_mappings(p, f, 1);
            sxs_emit_start_element(p, f);
            sxs_emit_end_element(p, f);
            sxs_emit_prefix_mappings(p, f, 0);
        }
        sxs_elem_pop(p);
        /* After self-close, we're back in the parent's content (or top-level
         * if this was the root). */
        if (p->elem_depth == 0) {
            p->state = SAX_ST_TOPLEVEL;  /* Will skip whitespace + finish. */
        } else {
            p->state = SAX_ST_ELEM_CONTENT;
        }
        return SAX_STEP_OK;
    }

    /* '/' at end of input: ambiguous. */
    if (c == '/' && p->pos + 1 >= p->end) {
        return is_final ? (sxs_set_error(p, "Unterminated opening tag"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }

    /* Attribute name. */
    p->state = SAX_ST_ATTR_NAME;
    sxs_carry_reset(p);
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ATTR_NAME
 * Scan attribute name.
 * ============================================================================ */

static int sxs_step_attr_name(TaurusSAXParser* p, int is_final) {
    if (p->carry_len == 0) {
        if (p->pos >= p->end) {
            return is_final ? (sxs_set_error(p, "Expected attribute name"), SAX_STEP_ERR)
                            : SAX_STEP_NEED_MORE;
        }
        if (!sxs_is_name_start(*p->pos)) {
            sxs_set_error(p, "Expected attribute name");
            return SAX_STEP_ERR;
        }
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    while (p->pos < p->end && sxs_is_name_char(*p->pos)) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    if (p->pos >= p->end) {
        if (!is_final) return SAX_STEP_NEED_MORE;
    }

    const char* name = sxs_scratch_from_carry(p);
    if (!name) { sxs_set_error(p, "out of memory"); return SAX_STEP_ERR; }
    /* Stash on the parser; ATTR_VALUE will pair it with the value. */
    p->pending_attr_name = name;
    sxs_carry_reset(p);
    p->state = SAX_ST_ATTR_EQ;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ATTR_EQ
 * Skip whitespace, expect '='.  After '=', transition to ATTR_VALUE_QUOTE.
 * ============================================================================ */

static int sxs_step_attr_eq(TaurusSAXParser* p, int is_final) {
    while (p->pos < p->end && sxs_is_ws(*p->pos)) p->pos++;
    if (p->pos >= p->end) {
        return is_final ? (sxs_set_error(p, "Expected '=' after attribute name"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }
    if (*p->pos != '=') {
        sxs_set_error(p, "Expected '=' after attribute name");
        return SAX_STEP_ERR;
    }
    p->pos++;
    p->state = SAX_ST_ATTR_VALUE_QUOTE;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ATTR_VALUE_QUOTE
 * Skip whitespace, expect opening quote; transition to ATTR_VALUE.
 * ============================================================================ */

static int sxs_step_attr_value_quote(TaurusSAXParser* p, int is_final) {
    while (p->pos < p->end && sxs_is_ws(*p->pos)) p->pos++;
    if (p->pos >= p->end) {
        return is_final ? (sxs_set_error(p, "Expected attribute value"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }
    if (!sxs_is_quote(*p->pos)) {
        sxs_set_error(p, "Expected quote for attribute value");
        return SAX_STEP_ERR;
    }
    /* Stash the quote char in carry[0] for ATTR_VALUE. */
    sxs_carry_reset(p);
    if (sxs_carry_putc(p, *p->pos) < 0) {
        sxs_set_error(p, "out of memory");
        return SAX_STEP_ERR;
    }
    p->pos++;
    p->state = SAX_ST_ATTR_VALUE;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ATTR_VALUE
 * Inside quoted attribute value.  carry[0] holds the quote char.
 * ============================================================================ */

static int sxs_step_attr_value(TaurusSAXParser* p, int is_final) {
    char quote = p->carry[0];
    /* p->carry[1..] may already have value bytes if we resumed mid-value. */
    while (p->pos < p->end && *p->pos != quote) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    if (p->pos >= p->end) {
        if (!is_final) return SAX_STEP_NEED_MORE;
        sxs_set_error(p, "Unterminated attribute value");
        return SAX_STEP_ERR;
    }
    /* p->pos points at closing quote. */
    p->pos++;
    /* Build value: skip carry[0] (quote), copy carry[1..]. */
    const char* value = sxs_scratch_append(p, p->carry + 1, p->carry_len - 1);
    if (!value) { sxs_set_error(p, "out of memory"); return SAX_STEP_ERR; }

    /* Pair pending_attr_name with value, append to top frame's attrs. */
    if (sxs_elem_add_attr(p, p->pending_attr_name, value) < 0) {
        sxs_set_error(p, "out of memory");
        return SAX_STEP_ERR;
    }
    p->pending_attr_name = NULL;
    sxs_carry_reset(p);
    p->state = SAX_ST_ATTR_LIST;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_ELEM_CONTENT
 * Between '>' and '</tag>'.  Dispatch: text / child element / comment /
 * CDATA / PI / closing tag.
 * ============================================================================ */

static int sxs_step_elem_content(TaurusSAXParser* p, int is_final) {
    if (p->pos >= p->end) {
        return is_final ? (sxs_set_error(p, "Unterminated element"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }

    if (*p->pos == '<') {
        /* Need at least 2 bytes to dispatch ('</', '<!', '<?', '<x'). */
        if (p->end - p->pos < 2) {
            if (!is_final) {
                if (sxs_carry_append(p, p->pos, (size_t)(p->end - p->pos)) < 0) {
                    sxs_set_error(p, "out of memory");
                    return SAX_STEP_ERR;
                }
                p->pos = p->end;
                return SAX_STEP_NEED_MORE;
            }
            sxs_set_error(p, "Unterminated element");
            return SAX_STEP_ERR;
        }
        char kind = p->pos[1];
        if (kind == '/') {
            p->pos += 2;  /* consume '</' */
            p->state = SAX_ST_CLOSING_TAG;
            sxs_carry_reset(p);
            return SAX_STEP_OK;
        }
        if (kind == '!') {
            /* Need 4 bytes for '<!--', 9 for '<![CDATA['. */
            if (p->end - p->pos < 4) {
                if (!is_final) {
                    if (sxs_carry_append(p, p->pos, (size_t)(p->end - p->pos)) < 0) {
                        sxs_set_error(p, "out of memory");
                        return SAX_STEP_ERR;
                    }
                    p->pos = p->end;
                    return SAX_STEP_NEED_MORE;
                }
                sxs_set_error(p, "Unterminated element");
                return SAX_STEP_ERR;
            }
            if (p->pos[2] == '-' && p->pos[3] == '-') {
                p->pos += 4;
                p->state = SAX_ST_COMMENT;
                sxs_carry_reset(p);
                return SAX_STEP_OK;
            }
            /* Not comment; must be CDATA ('<![CDATA['). Need 9 bytes. */
            if (p->end - p->pos < 9) {
                if (!is_final) {
                    if (sxs_carry_append(p, p->pos, (size_t)(p->end - p->pos)) < 0) {
                        sxs_set_error(p, "out of memory");
                        return SAX_STEP_ERR;
                    }
                    p->pos = p->end;
                    return SAX_STEP_NEED_MORE;
                }
                sxs_set_error(p, "Unterminated element");
                return SAX_STEP_ERR;
            }
            if (memcmp(p->pos, "<![CDATA[", 9) == 0) {
                p->pos += 9;
                p->state = SAX_ST_CDATA;
                sxs_carry_reset(p);
                return SAX_STEP_OK;
            }
            sxs_set_error(p, "Unexpected '<!' in content");
            return SAX_STEP_ERR;
        }
        if (kind == '?') {
            p->pos += 2;
            p->state = SAX_ST_PI;
            sxs_carry_reset(p);
            return SAX_STEP_OK;
        }
        /* Child element. */
        p->pos++;  /* consume '<' */
        p->state = SAX_ST_ELEM_OPEN_NAME;
        sxs_carry_reset(p);
        return SAX_STEP_OK;
    }

    /* Text content. */
    p->state = SAX_ST_TEXT;
    sxs_carry_reset(p);
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_TEXT
 * Scan text content, emit characters() in chunks (no carry for large text).
 * ============================================================================ */

static int sxs_step_text(TaurusSAXParser* p, int is_final) {
    const char* start = p->pos;
    while (p->pos < p->end && *p->pos != '<') p->pos++;
    size_t len = (size_t)(p->pos - start);
    if (len > 0) {
        sxs_emit_characters(p, start, len);
    }
    if (p->pos >= p->end) {
        /* End of input.  If is_final, the element was unterminated --
         * that's an error, but we've already emitted the text. */
        if (is_final) {
            sxs_set_error(p, "Unterminated element");
            return SAX_STEP_ERR;
        }
        return SAX_STEP_NEED_MORE;
    }
    /* p->pos points at '<' — back to content dispatch. */
    p->state = SAX_ST_ELEM_CONTENT;
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_CLOSING_TAG
 * Inside '</'.  Scan name, match against top of elem_stack, skip '>'.
 * ============================================================================ */

static int sxs_step_closing_tag(TaurusSAXParser* p, int is_final) {
    /* Name. */
    if (p->carry_len == 0) {
        if (p->pos >= p->end) {
            return is_final ? (sxs_set_error(p, "Expected closing tag name"), SAX_STEP_ERR)
                            : SAX_STEP_NEED_MORE;
        }
        if (!sxs_is_name_start(*p->pos)) {
            sxs_set_error(p, "Expected closing tag name");
            return SAX_STEP_ERR;
        }
        sxs_carry_putc(p, *p->pos);
        p->pos++;
    }
    while (p->pos < p->end && sxs_is_name_char(*p->pos)) {
        sxs_carry_putc(p, *p->pos);
        p->pos++;
    }
    if (p->pos >= p->end) {
        if (!is_final) return SAX_STEP_NEED_MORE;
    }
    /* Match against top of stack. */
    SaxElementFrame* f = sxs_elem_top(p);
    if (!f) {
        sxs_set_error(p, "Closing tag without matching open");
        return SAX_STEP_ERR;
    }
    if (p->carry_len != f->name_len ||
        memcmp(p->carry, f->name, f->name_len) != 0) {
        sxs_set_error(p, "Mismatched closing tag");
        return SAX_STEP_ERR;
    }
    sxs_carry_reset(p);

    /* Skip whitespace + '>'. */
    while (p->pos < p->end && sxs_is_ws(*p->pos)) p->pos++;
    if (p->pos >= p->end) {
        return is_final ? (sxs_set_error(p, "Unterminated closing tag"), SAX_STEP_ERR)
                        : SAX_STEP_NEED_MORE;
    }
    if (*p->pos != '>') {
        sxs_set_error(p, "Expected '>' after closing tag name");
        return SAX_STEP_ERR;
    }
    p->pos++;

    /* Emit end_element + prefix mappings. */
    sxs_emit_end_element(p, f);
    sxs_emit_prefix_mappings(p, f, 0);
    sxs_elem_pop(p);

    /* After closing, back to content (or top-level if root just closed). */
    if (p->elem_depth == 0) {
        p->state = SAX_ST_TOPLEVEL;
    } else {
        p->state = SAX_ST_ELEM_CONTENT;
    }
    return SAX_STEP_OK;
}

/* ============================================================================
 * State: SAX_ST_COMMENT (body content)
 * Scan to '-->', emit comment event.
 * ============================================================================ */

static int sxs_step_comment(TaurusSAXParser* p, int is_final) {
    /* Boundary: `-->` straddling carry and new input. */
    if (p->carry_len >= 2 && p->pos < p->end &&
        p->carry[p->carry_len - 2] == '-' &&
        p->carry[p->carry_len - 1] == '-' &&
        *p->pos == '>') {
        p->carry_len -= 2;
        p->pos++;
        if (p->carry_len > 0) {
            const char* c = sxs_scratch_from_carry(p);
            if (c && p->handler && p->handler->comment) {
                p->handler->comment(p->user_data, c);
            }
        }
        sxs_carry_reset(p);
        p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
        return SAX_STEP_OK;
    }
    if (p->carry_len >= 1 && p->pos + 1 < p->end &&
        p->carry[p->carry_len - 1] == '-' &&
        p->pos[0] == '-' && p->pos[1] == '>') {
        p->carry_len -= 1;
        p->pos += 2;
        if (p->carry_len > 0) {
            const char* c = sxs_scratch_from_carry(p);
            if (c && p->handler && p->handler->comment) {
                p->handler->comment(p->user_data, c);
            }
        }
        sxs_carry_reset(p);
        p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
        return SAX_STEP_OK;
    }

    while (p->pos + 2 < p->end) {
        if (p->pos[0] == '-' && p->pos[1] == '-' && p->pos[2] == '>') {
            if (p->carry_len > 0) {
                const char* c = sxs_scratch_from_carry(p);
                if (c && p->handler && p->handler->comment) {
                    p->handler->comment(p->user_data, c);
                }
            }
            p->pos += 3;
            sxs_carry_reset(p);
            p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
            return SAX_STEP_OK;
        }
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    while (p->pos < p->end) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    if (is_final) {
        sxs_set_error(p, "Unterminated comment");
        return SAX_STEP_ERR;
    }
    return SAX_STEP_NEED_MORE;
}

/* ============================================================================
 * State: SAX_ST_CDATA
 * Scan to ']]>', emit cdata event.
 *
 * Boundary handling: the closing `]]>` may straddle carry and new input
 * (a `]` at end of one chunk + `]>` at start of the next).  Before the
 * normal scan, check whether the last 1-2 bytes of carry + the first
 * 1-2 bytes of input form `]]>`.
 * ============================================================================ */

static int sxs_step_cdata(TaurusSAXParser* p, int is_final) {
    /* Boundary check: does `]]>` straddle carry and new input? */
    if (p->carry_len >= 2 && p->pos < p->end &&
        p->carry[p->carry_len - 2] == ']' &&
        p->carry[p->carry_len - 1] == ']' &&
        *p->pos == '>') {
        p->carry_len -= 2;  /* strip trailing ']]' from body */
        p->pos++;           /* consume '>' */
        if (p->carry_len > 0) {
            const char* c = sxs_scratch_from_carry(p);
            if (c && p->handler && p->handler->cdata) {
                p->handler->cdata(p->user_data, c);
            }
        }
        sxs_carry_reset(p);
        p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
        return SAX_STEP_OK;
    }
    if (p->carry_len >= 1 && p->pos + 1 < p->end &&
        p->carry[p->carry_len - 1] == ']' &&
        p->pos[0] == ']' && p->pos[1] == '>') {
        p->carry_len -= 1;  /* strip trailing ']' from body */
        p->pos += 2;        /* consume ']>' */
        if (p->carry_len > 0) {
            const char* c = sxs_scratch_from_carry(p);
            if (c && p->handler && p->handler->cdata) {
                p->handler->cdata(p->user_data, c);
            }
        }
        sxs_carry_reset(p);
        p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
        return SAX_STEP_OK;
    }

    /* Normal scan within new input. */
    while (p->pos + 2 < p->end) {
        if (p->pos[0] == ']' && p->pos[1] == ']' && p->pos[2] == '>') {
            if (p->carry_len > 0) {
                const char* c = sxs_scratch_from_carry(p);
                if (c && p->handler && p->handler->cdata) {
                    p->handler->cdata(p->user_data, c);
                }
            }
            p->pos += 3;
            sxs_carry_reset(p);
            p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
            return SAX_STEP_OK;
        }
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    /* Trailing 0-2 bytes might be start of `]]>`; save them. */
    while (p->pos < p->end) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    if (is_final) {
        sxs_set_error(p, "Unterminated CDATA");
        return SAX_STEP_ERR;
    }
    return SAX_STEP_NEED_MORE;
}

/* ============================================================================
 * State: SAX_ST_PI
 * Scan to '?>', parse target + data, emit PI event.
 * ============================================================================ */

static int sxs_step_pi(TaurusSAXParser* p, int is_final) {
    /* Boundary: `?>` straddling carry and new input. */
    if (p->carry_len >= 1 && p->pos < p->end &&
        p->carry[p->carry_len - 1] == '?' && *p->pos == '>') {
        p->carry_len -= 1;  /* strip trailing '?' */
        p->pos++;           /* consume '>' */
        const char* ts = p->carry;
        const char* te = ts;
        while (te < p->carry + p->carry_len && !sxs_is_ws(*te)) te++;
        const char* target = sxs_scratch_append(p, ts, (size_t)(te - ts));
        const char* ds = te;
        while (ds < p->carry + p->carry_len && sxs_is_ws(*ds)) ds++;
        const char* data = sxs_scratch_append(p, ds, (size_t)(p->carry + p->carry_len - ds));
        if (p->handler && p->handler->processing_instruction) {
            p->handler->processing_instruction(p->user_data, target, data);
        }
        sxs_carry_reset(p);
        p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
        return SAX_STEP_OK;
    }

    /* Drain until '?>'. */
    while (p->pos + 1 < p->end) {
        if (p->pos[0] == '?' && p->pos[1] == '>') {
            /* Parse target + data from carry. */
            const char* ts = p->carry;
            const char* te = ts;
            while (te < p->carry + p->carry_len && !sxs_is_ws(*te)) te++;
            const char* target = sxs_scratch_append(p, ts, (size_t)(te - ts));
            const char* ds = te;
            while (ds < p->carry + p->carry_len && sxs_is_ws(*ds)) ds++;
            const char* data = sxs_scratch_append(p, ds, (size_t)(p->carry + p->carry_len - ds));
            if (p->handler && p->handler->processing_instruction) {
                p->handler->processing_instruction(p->user_data, target, data);
            }
            p->pos += 2;
            sxs_carry_reset(p);
            p->state = (p->elem_depth > 0) ? SAX_ST_ELEM_CONTENT : SAX_ST_TOPLEVEL;
            return SAX_STEP_OK;
        }
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    while (p->pos < p->end) {
        if (sxs_carry_putc(p, *p->pos) < 0) {
            sxs_set_error(p, "out of memory");
            return SAX_STEP_ERR;
        }
        p->pos++;
    }
    if (is_final) {
        sxs_set_error(p, "Unterminated PI");
        return SAX_STEP_ERR;
    }
    return SAX_STEP_NEED_MORE;
}

/* ============================================================================
 * Dispatcher
 * ============================================================================ */

static int sxs_dispatch(TaurusSAXParser* p, int is_final) {
    for (;;) {
        int rc;
        switch (p->state) {
            case SAX_ST_TOPLEVEL:        rc = sxs_step_top(p, is_final); break;
            case SAX_ST_ELEM_OPEN_NAME:  rc = sxs_step_elem_open_name(p, is_final); break;
            case SAX_ST_ATTR_LIST:       rc = sxs_step_attr_list(p, is_final); break;
            case SAX_ST_ATTR_NAME:       rc = sxs_step_attr_name(p, is_final); break;
            case SAX_ST_ATTR_EQ:         rc = sxs_step_attr_eq(p, is_final); break;
            case SAX_ST_ATTR_VALUE_QUOTE: rc = sxs_step_attr_value_quote(p, is_final); break;
            case SAX_ST_ATTR_VALUE:      rc = sxs_step_attr_value(p, is_final); break;
            case SAX_ST_ELEM_CONTENT:    rc = sxs_step_elem_content(p, is_final); break;
            case SAX_ST_TEXT:            rc = sxs_step_text(p, is_final); break;
            case SAX_ST_CLOSING_TAG:     rc = sxs_step_closing_tag(p, is_final); break;
            case SAX_ST_COMMENT:         rc = sxs_step_comment(p, is_final); break;
            case SAX_ST_CDATA:           rc = sxs_step_cdata(p, is_final); break;
            case SAX_ST_PI:              rc = sxs_step_pi(p, is_final); break;
            case SAX_ST_DONE:            return 0;
            case SAX_ST_ERROR:           return -1;
            default:
                sxs_set_error(p, "Internal: unknown state");
                return -1;
        }

        if (rc == SAX_STEP_ERR) return -1;
        if (rc == SAX_STEP_NEED_MORE) return 0;  /* Suspend; more input coming. */

        /* OK or DONE: keep dispatching.  TOPLEVEL termination: when
         * elem_depth returns to 0 and we're in TOPLEVEL with no more
         * input, we're done. */
        if (p->state == SAX_ST_TOPLEVEL && p->elem_depth == 0 &&
            p->start_emitted && p->pos >= p->end) {
            if (is_final) {
                sxs_emit_end_document(p);
                p->state = SAX_ST_DONE;
                return 0;
            }
            return 0;
        }
    }
    /* unreachable */
}

/* ============================================================================
 * Public entry points (called from parser.c)
 * ============================================================================ */

/* Called from taurus_sax_parser_feed when streaming flag is set.
 *
 * Appends new chunk to input_buf (we still need a contiguous buffer
 * for pointer-based scanning), then resumes the state machine.
 *
 * Returns 0 on success / suspended, -1 on parse error. */
int taurus_sax_streaming_feed(TaurusSAXParser* p,
                              const char* xml, size_t len,
                              int is_final) {
    /* Emit start_document on the very first chunk. */
    sxs_emit_start_document(p);

    /* If the previous step stashed partial bytes in carry because
     * there weren't enough to disambiguate (TOPLEVEL needs up to 9
     * for "<![CDATA[", ELEM_CONTENT needs up to 9 for the same in
     * body position), prepend them to the new input so the
     * dispatcher sees a contiguous stream.  Carry is otherwise used
     * as "in-progress token" by ATTR_NAME, ATTR_VALUE, COMMENT body,
     * etc. and must NOT be touched. */
    if (p->carry_len > 0 &&
        (p->state == SAX_ST_TOPLEVEL || p->state == SAX_ST_ELEM_CONTENT)) {
        if (p->input_len + p->carry_len + len > p->input_cap) {
            size_t need = p->input_len + p->carry_len + len;
            size_t new_cap = p->input_cap ? p->input_cap : 256;
            while (new_cap < need) new_cap *= 2;
            char* grown = (char*)realloc(p->input_buf, new_cap);
            if (!grown) { sxs_set_error(p, "out of memory"); return -1; }
            p->input_buf = grown;
            p->input_cap = new_cap;
        }
        /* Shift existing unscanned bytes right by carry_len. */
        if (p->input_len > 0) {
            memmove(p->input_buf + p->carry_len,
                    p->input_buf,
                    p->input_len);
        }
        memcpy(p->input_buf, p->carry, p->carry_len);
        p->input_len += p->carry_len;
        sxs_carry_reset(p);
    }

    /* Append the new chunk. */
    if (len > 0) {
        if (p->input_len + len > p->input_cap) {
            size_t new_cap = p->input_cap ? p->input_cap : 256;
            while (new_cap < p->input_len + len) new_cap *= 2;
            char* grown = (char*)realloc(p->input_buf, new_cap);
            if (!grown) {
                sxs_set_error(p, "out of memory");
                return -1;
            }
            p->input_buf = grown;
            p->input_cap = new_cap;
        }
        memcpy(p->input_buf + p->input_len, xml, len);
        p->input_len += len;
    }

    /* Resume from consumed_offset; pos..end covers unscanned bytes. */
    p->pos = p->input_buf + p->consumed_offset;
    p->end = p->input_buf + p->input_len;

    int rc = sxs_dispatch(p, is_final);

    /* Save the new consumed offset; compact the buffer so the next
     * feed starts at offset 0. */
    size_t consumed = (size_t)(p->pos - p->input_buf);
    size_t remaining = p->input_len - consumed;
    if (consumed > 0 && remaining > 0) {
        memmove(p->input_buf, p->pos, remaining);
    }
    p->input_len = remaining;
    p->consumed_offset = 0;

    /* Detect end-of-stream: dispatch said OK and we're back in
     * TOPLEVEL with no open elements and no buffered input. */
    if (rc == 0 && is_final && p->state == SAX_ST_TOPLEVEL &&
        p->elem_depth == 0 && p->input_len == 0 && !p->end_emitted) {
        sxs_emit_end_document(p);
        p->state = SAX_ST_DONE;
    }

    return rc;
}

/* Called from taurus_sax_parser_free to release streaming resources. */
void taurus_sax_streaming_reset(TaurusSAXParser* p) {
    while (p->elem_depth > 0) sxs_elem_pop(p);
    sxs_carry_reset(p);
    free(p->carry);
    p->carry = NULL;
    p->carry_cap = 0;
    /* scratch is freed by parser.c's free path. */
}

int taurus_sax_parser_set_streaming(TaurusSAXParser* parser, int streaming) {
    if (!parser) return -1;
    parser->streaming = streaming ? 1 : 0;
    return 0;
}
