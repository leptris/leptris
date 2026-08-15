/* flat/direct_parse.c — Single-pass parse into TaurusElement tree
 * (TODO 147 Phase A).
 *
 * This is the pugixml-style parser: scan XML once, write TaurusElement
 * records directly. No FlatDoc intermediate, no promote pass.
 *
 * Key techniques:
 *   1. Upfront bulk allocation: estimate element count, malloc one block
 *   2. Zero-copy names: NUL-terminate in-place in the buffer copy
 *   3. Direct edge wiring: set compact-pointer offsets via pointer
 *      arithmetic (no function call, no overflow table)
 *   4. Lookup tables: single-byte char classification (no branch chains)
 *   5. memchr for text scanning: libc vectorized search
 *
 * The parser handles: elements (open/close/self-closing), attributes
 * (single+double quoted), text, comments, CDATA, PIs, XML declaration,
 * DOCTYPE skipping, BOM. Namespace-aware: splits prefix:local names,
 * moves xmlns declarations to elem->namespaces.
 *
 * For inputs the direct parser can't handle (DTD internal subset with
 * entities), the caller falls back to flat_parse + promote.
 */
#include "direct_parse.h"
#include "../dom/element.h"
#include "../dom/root_doc_map.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/doctype.h"
#include "../common/string_view.h"
#include "../common/chartype.h"
#include "../common/entities.h"
#include "../common/port.h"
#include "../common/simd_text.h"
#include "../dtd/model.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern TAURUS_THREAD_LOCAL int g_taurus_strict_mode;
extern void taurus_compact_set_current_document(struct taurus_document* doc);
int taurus_element_add_namespace(struct taurus_element* elem,
                                  struct taurus_namespace* ns);

#define DP_MAX_DEPTH 256

typedef struct {
    char* buf;
    char* pos;
    char* end;
    TaurusMemoryPool* pool;
    struct taurus_document* doc;
    TaurusElement open_stack[DP_MAX_DEPTH];
    /* TODO 155 Phase C: per-depth last-child cache. Replaces the
     * last_child_off field on element. last_child_stack[i] holds
     * the most recently wired child of open_stack[i], or NULL when
     * no child has been wired yet at that depth. */
    TaurusNode* last_child_stack[DP_MAX_DEPTH];
    int depth;
    TaurusElement root;
    struct taurus_processing_instruction* pis_head;
    struct taurus_processing_instruction* pis_tail;
    /* XML declaration fields */
    char* version;
    char* encoding;
    int standalone;
    /* Source line tracking (issue #223). Updated as the scanner
     * crosses '\n' bytes. Frozen into each node at creation. */
    uint32_t line;
    int had_declaration;
    /* Bulk-allocated attribute block. Pre-allocated from pool so the
     * common case is a bump-pointer off the block — no per-attr
     * pool_alloc, no name interning, no value pool_strdup. Overflow
     * falls back to per-attr pool_alloc. */
    struct taurus_attribute* attr_block;
    size_t attr_idx;
    size_t attr_capacity;
    /* TODO 159 Phase G: parser-local last-attr cache for the element
     * currently being parsed. Eliminates the O(N) walk-to-find-tail
     * in dp_add_attr_inline — O(1) wiring per attr instead of O(N),
     * removing the O(N²) per-element-attrs cost. Reset to NULL at
     * the start of each dp_parse_attrs call.
     *
     * Also caches the last xmlns ns-node for the same reason — the
     * old code walked the ns list to find the tail on every xmlns. */
    struct taurus_attribute* current_elem_last_attr;
    struct taurus_namespace* current_elem_last_ns;
    /* DTD parsed from the DOCTYPE internal subset. NULL when the
     * document has no DTD (or only an external subset). When non-NULL,
     * text/attr entity expansion routes through
     * taurus_decode_entities_view_with_dtd so custom entities
     * (&foo; where foo is declared in the DTD) resolve correctly. */
    TaurusDTD* dtd;
} DParser;

static inline void dp_skip_ws(DParser* p) {
    while (p->pos < p->end && IS_WS(*p->pos)) {
        if (*p->pos == '\n') p->line++;
        p->pos++;
    }
}

/* Phase B (TODO 166): name-char scan helper.
 *
 * Originally prototyped a 4-byte ASCII fast path
 * (`(w & 0x80808080u) == 0` guard + 4 IS_NAME_CHAR checks per
 * iteration). On the many-attrs benchmark (avg attr-name length
 * 7), the fast path added MORE work per 4 chars than the byte
 * loop saved: the memcpy + mask check + 4 byte extractions cost
 * more than 4 byte loads, while modern branch predictors already
 * make the byte loop nearly free. Measured ~25% regression at
 * K=50 attrs/element. Reverted to the plain byte loop.
 *
 * Kept as a thin TAURUS_ALWAYS_INLINE wrapper — preserves the
 * call-site DRY (six name-scan loops collapse to one helper)
 * and guarantees no call-site regression vs the original
 * inline loop. */
static TAURUS_ALWAYS_INLINE void dp_scan_name(DParser* p) {
    while (p->pos < p->end && IS_NAME_CHAR(*p->pos))
        p->pos++;
}

/* Tally '\n' bytes in [from, to) and fold into p->line. Used after
 * bulk scans (memchr for text, multi-char literal matches) where the
 * per-byte scanner can't update line inline. */
static inline void dp_advance_line(DParser* p, char* from, char* to) {
    for (char* c = from; c < to; c++) {
        if (*c == '\n') p->line++;
    }
}

/* Compile-time offset tables for next_sibling and parent_off
 * across all node types. Eliminates the type-dispatched switch in
 * dp_wire_child — one array lookup + one store replaces 5-way branch.
 * TODO 158: branchless tree wiring.
 *
 * TODO 179 Phase B: text/comment/cdata/pi siblings migrated to cp16
 * (2-byte compact pointer). Element still uses int32 — split tables. */
static const size_t dp_ns_off_int32[1] = {
    offsetof(struct taurus_element,  next_sibling_off),
};
static const size_t dp_ns_off_cp16[4] = {
    offsetof(TaurusTextNode,        next_sibling_cp),
    offsetof(TaurusCommentNode,     next_sibling_cp),
    offsetof(TaurusCDATANode,       next_sibling_cp),
    offsetof(TaurusPINode,          next_sibling_cp),
};
static const size_t dp_par_off[5] = {
    offsetof(struct taurus_element,  parent_off),
    offsetof(TaurusTextNode,        parent_off),
    offsetof(TaurusCommentNode,     parent_off),
    offsetof(TaurusCDATANode,       parent_off),
    offsetof(TaurusPINode,          parent_off),
};

/* Wire child into parent's child chain. Uses compile-time offset
 * tables for branchless type dispatch — no switch, no branch predict. */
static inline void dp_wire_child(DParser* p, TaurusElement parent,
                                  TaurusNode* child) {
    int32_t parent_to_child = (int32_t)((char*)parent - (char*)child);
    unsigned t = (unsigned)child->type;
    if (t < 5) {
        *(int32_t*)((char*)child + dp_par_off[t]) = parent_to_child;
    }

    TaurusNode* prev_last = p->last_child_stack[p->depth - 1];
    if (prev_last) {
        unsigned pt = (unsigned)prev_last->type;
        if (pt == TAURUS_NODE_TYPE_ELEMENT) {
            /* Element siblings: int32 byte offset. */
            int32_t sib_off = (int32_t)((char*)child - (char*)prev_last);
            *(int32_t*)((char*)prev_last + dp_ns_off_int32[0]) = sib_off;
        } else if (pt >= TAURUS_NODE_TYPE_TEXT && pt <= TAURUS_NODE_TYPE_PI) {
            /* text/comment/cdata/pi siblings: cp16 compact pointer.
             * Direct store (no overflow table) — direct_parse is
             * overflow-free by design; cp16's ±256 KB range covers
             * any realistic document. */
            int16_t* field = (int16_t*)((char*)prev_last + dp_ns_off_cp16[pt - 1]);
            ptrdiff_t d = (char*)child - (char*)prev_last;
            *field = (int16_t)(d >> 3);
        }
    } else {
        int32_t child_off = (int32_t)((char*)child - (char*)parent);
        parent->first_child_off = child_off;
    }
    p->last_child_stack[p->depth - 1] = child;

    if (t == TAURUS_NODE_TYPE_ELEMENT) {
        parent->child_count++;
    }
}

/* Inline attribute allocation. Takes the next slot from the
 * pre-allocated attr_block (bump pointer) when capacity remains,
 * else falls back to a per-attr pool_alloc. Both name and value
 * are zero-copy pointers into the buffer copy (already NUL-
 * terminated in-place by the caller). Skips name interning and
 * value pool_strdup entirely — direct_parse path excludes entity
 * inputs, so has_entities is always 0. */
static inline int dp_add_attr_inline(DParser* p, TaurusElement elem,
                                      char* name, size_t name_len,
                                      char* val, size_t val_len,
                                      int has_amp) {
    struct taurus_attribute* attr;
    if (p->attr_idx < p->attr_capacity) {
        attr = &p->attr_block[p->attr_idx++];
    } else {
        attr = (struct taurus_attribute*)taurus_pool_alloc(
            p->pool, sizeof(struct taurus_attribute));
        if (!attr) return -1;
    }

    attr->name_view = taurus_sv_from_ptr(name, name_len);
    attr->value_view = taurus_sv_from_ptr(val, val_len);
    attr->name = name;            /* zero-copy, NUL-terminated in buffer */
    /* Entity handling for attr values (has_amp comes from the caller's
     * fused value scan — TODO 184: one pass finds the closing quote AND
     * flags '&', replacing the two-pass quote-memchr + amp scan):
     * - DTD present + value has '&': eagerly expand via DTD-aware
     *   decoder (custom entities &foo; need the DTD table). Result
     *   is pool-allocated; has_entities=0 (already resolved).
     * - No DTD + value has '&': leave value NULL, has_entities=1.
     *   Accessor expands predefined entities lazily on first read.
     * - No '&': zero-copy, no expansion needed. */
    if (val_len > 0 && has_amp) {
        if (p->dtd) {
            TaurusStringView dsv = taurus_sv_from_ptr(val, val_len);
            char* expanded = taurus_decode_entities_view_with_dtd(
                &dsv, p->dtd, p->pool);
            if (expanded) {
                attr->value = expanded;
                attr->has_entities = 0;
            } else {
                attr->value = NULL;
                attr->has_entities = 1;
            }
        } else {
            attr->value = NULL;
            attr->has_entities = 1;
        }
    } else {
        attr->value = val;
        attr->has_entities = 0;
    }
    attr->ns_cache = NULL;  /* TODO 173: side cache allocated on demand */
    /* TODO 183 Phase 5 (TODO 181 Phase D): cp16 sibling edge. Attrs of
     * one element are adjacent attr_block slots (distance ≤ K × 64 B,
     * far inside cp16 range) — direct store, no encoder call on the
     * hot path. The defensive walk below uses the decoder. */
    attr->next_cp = 0;

    /* FNV-1a hash deferred to first read via attr_name_hash() (TODO 172).
     * Saves ~5ns per attr on attr-heavy parse paths where XPath attr
     * predicates are never used. The first read computes and caches. */
    attr->name_hash = 0;

    /* Wire attr into elem's attr list. TODO 159 Phase G: use the
     * parser-local last-attr cache for O(1) wiring instead of walking
     * to find the tail (was O(N) per attr, O(N²) per element).
     *
     * Careful: first_attribute_off == 0 means empty list. We can't decode
     * the pointer and check for NULL — at offset 0 the decoded pointer
     * is `elem` itself, which is non-NULL. Check the offset field. */
    int32_t attr_off = (int32_t)((char*)attr - (char*)elem);
    if (elem->first_attribute_off != 0) {
        /* Cache should always be valid mid-parse. Fall back to walk
         * only if cache is NULL (defensive — shouldn't happen). */
        struct taurus_attribute* tail = p->current_elem_last_attr;
        if (tail) {
            /* Same-element attrs are adjacent block slots — direct
             * cp16 store, no encoder call (TODO 183 Phase 5). */
            tail->next_cp = (int16_t)(((char*)attr - (char*)tail) >> 3);
        } else {
            tail = (struct taurus_attribute*)((char*)elem + elem->first_attribute_off);
            while (taurus_attr_next(tail)) tail = taurus_attr_next(tail);
            taurus_attr_set_next(tail, attr);
        }
    } else {
        elem->first_attribute_off = attr_off;
    }
    p->current_elem_last_attr = attr;
    elem->attr_count++;
    return 0;
}

/* Handle `<!DOCTYPE ...>` markup. Extracted as a cold path
 * (TODO 166 Phase A): runs 0–1 times per document, but the body is
 * ~140 lines including PUBLIC/SYSTEM re-scan, internal-subset
 * extraction, and DTD parsing. Keeping it inline bloats the hot
 * parse loop's i-cache footprint for no benefit.
 *
 * Returns:
 *   0  — handled, caller should `continue` the parse loop.
 *  -1  — parse error, caller should fail. */
static TAURUS_NOINLINE int dp_parse_doctype(DParser* p,
                                             uint32_t markup_line) {
    (void)markup_line;
    char* doctype_start = p->pos;
    p->pos += 2; /* skip "<!" */
    /* Match "DOCTYPE" keyword (case-sensitive per XML spec). */
    if (p->end - p->pos < 7 ||
        memcmp(p->pos, "DOCTYPE", 7) != 0) {
        while (p->pos < p->end && *p->pos != '>') p->pos++;
        if (p->pos < p->end) p->pos++;
        dp_advance_line(p, doctype_start, p->pos);
        return 0;
    }
    p->pos += 7;
    while (p->pos < p->end && IS_WS(*p->pos)) {
        if (*p->pos == '\n') p->line++;
        p->pos++;
    }
    /* Scan DOCTYPE name. */
    char* dt_name_start = p->pos;
    dp_scan_name(p);
    size_t dt_name_len = p->pos - dt_name_start;

    /* Skip to '[' (internal subset) or '>' (no subset). */
    char* subset_start = NULL;
    char* subset_end = NULL;
    while (p->pos < p->end && *p->pos != '>' && *p->pos != '[') {
        p->pos++;
    }
    if (p->pos < p->end && *p->pos == '[') {
        subset_start = ++p->pos;
        /* Find matching ']'. Nested brackets aren't legal
         * in DTD internal subsets, so no depth tracking. */
        while (p->pos < p->end && *p->pos != ']') p->pos++;
        subset_end = p->pos;
        if (p->pos < p->end) p->pos++; /* skip ']' */
        /* Skip to '>'. */
        while (p->pos < p->end && *p->pos != '>') p->pos++;
        if (p->pos < p->end) p->pos++; /* skip '>' */
    } else {
        /* No internal subset, skip to '>'. */
        while (p->pos < p->end && *p->pos != '>') p->pos++;
        if (p->pos < p->end) p->pos++; /* skip '>' */
    }

    /* Create DOCTYPE node with the extracted name. */
    TaurusDoctypeNode* dt = NULL;
    if (dt_name_len > 0) {
        dt = taurus_doctype_create(
            dt_name_start, dt_name_len, p->pool);
        if (dt) {
            p->doc->doctype = dt;
        }
    }

    /* Parse PUBLIC/SYSTEM identifiers (issue #253).
     * DOCTYPE grammar (XML 1.0 §2.8):
     *   <!DOCTYPE name (SYSTEM quoted | PUBLIC quoted quoted)? subset?>
     * The keywords and quoted strings sit between the name
     * and the '[' or '>'. We need to re-scan that region
     * because the initial skip above skipped past them
     * without extracting values. */
    /* Re-scan from after the name to find PUBLIC/SYSTEM. */
    char* scan = dt_name_start + dt_name_len;
    /* Skip whitespace after name. */
    while (scan < p->end && IS_WS(*scan)) scan++;
    char* public_id = NULL;
    size_t public_id_len = 0;
    char* system_id = NULL;
    size_t system_id_len = 0;
    if (scan + 6 <= p->end && memcmp(scan, "SYSTEM", 6) == 0) {
        scan += 6;
        while (scan < p->end && IS_WS(*scan)) scan++;
        if (scan < p->end && (*scan == '"' || *scan == '\'')) {
            char q = *scan++;
            system_id = scan;
            while (scan < p->end && *scan != q) scan++;
            system_id_len = scan - system_id;
            if (scan < p->end) scan++; /* skip closing quote */
        }
    } else if (scan + 6 <= p->end && memcmp(scan, "PUBLIC", 6) == 0) {
        scan += 6;
        while (scan < p->end && IS_WS(*scan)) scan++;
        /* Public ID (quoted) */
        if (scan < p->end && (*scan == '"' || *scan == '\'')) {
            char q = *scan++;
            public_id = scan;
            while (scan < p->end && *scan != q) scan++;
            public_id_len = scan - public_id;
            if (scan < p->end) scan++; /* skip closing quote */
        }
        /* System ID (quoted, after whitespace) */
        while (scan < p->end && IS_WS(*scan)) scan++;
        if (scan < p->end && (*scan == '"' || *scan == '\'')) {
            char q = *scan++;
            system_id = scan;
            while (scan < p->end && *scan != q) scan++;
            system_id_len = scan - system_id;
            if (scan < p->end) scan++; /* skip closing quote */
        }
    }
    /* NUL-terminate and set on the DOCTYPE node. The
     * values point into the mutable buffer copy. */
    if (dt) {
        if (public_id && public_id_len > 0) {
            public_id[public_id_len] = '\0';
            taurus_doctype_set_public_id(dt, public_id, p->pool);
        }
        if (system_id && system_id_len > 0) {
            system_id[system_id_len] = '\0';
            taurus_doctype_set_system_id(dt, system_id, p->pool);
        }
    }

    /* Parse internal subset if non-empty — builds the
     * entity table for custom entity expansion, and
     * stores the raw text on the DOCTYPE node so the
     * public API (taurus_doctype_get_internal_subset)
     * returns it (#253). */
    if (subset_start && subset_end > subset_start) {
        /* NUL-terminate the subset text in the buffer. */
        *subset_end = '\0';
        if (dt) {
            taurus_doctype_set_internal_subset(
                dt, subset_start, p->pool);
        }
        TaurusDTD* dtd = taurus_dtd_parse_internal_subset(
            subset_start, (size_t)(subset_end - subset_start),
            p->pool);
        if (dtd) {
            p->dtd = dtd;
        }
    }
    dp_advance_line(p, doctype_start, p->pos);
    return 0;
}

/* Parse attributes for an element. Writes attr structs into the
 * pre-allocated attr_block (zero-copy name/value, no interning).
 * Names are NUL-terminated in-place AFTER '=' is consumed; values
 * are NUL-terminated in-place at the closing quote. */
static int dp_parse_attrs(DParser* p, TaurusElement elem) {
    /* Reset the per-element caches so dp_add_attr_inline and the
     * xmlns wiring below can both run in O(1) per attr. */
    p->current_elem_last_attr = NULL;
    p->current_elem_last_ns = NULL;
    while (p->pos < p->end) {
        dp_skip_ws(p);
        if (p->pos >= p->end) return -1;
        char c = *p->pos;
        if (c == '>') { p->pos++; return 0; }
        if (c == '/') {
            if (p->pos + 1 >= p->end || p->pos[1] != '>') return -1;
            p->pos += 2;
            return 1; /* self-closing */
        }

        /* Attribute name — scan as (pointer, length), no NUL-term. */
        char* name_start = p->pos;
        if (!IS_NAME_START(*p->pos)) return -1;
        p->pos++;
        dp_scan_name(p);
        char* name_end = p->pos;
        size_t name_len = name_end - name_start;
        /* Defer NUL-termination until after '=' is consumed — the
         * delimiter byte (whitespace or '=') is needed for the scan. */

        /* Skip = and whitespace. */
        dp_skip_ws(p);
        if (p->pos >= p->end || *p->pos != '=') return -1;
        p->pos++;
        /* '=' consumed — the delimiter byte at name_end is no longer
         * needed. Safe to NUL-terminate the name in-place now. */
        *name_end = '\0';
        dp_skip_ws(p);

        /* Quoted value — FUSED scan (TODO 184): one pass finds the
         * closing quote AND flags '&' for entity routing. Replaces
         * quote-memchr + separate amp check (two passes; memchr pays
         * ~10ns setup even on 6-byte values — the TODO 174 finding,
         * applied to the quote scan too).
         *
         * First 48 bytes inline (byte loop beats memchr setup — the
         * common case: values are 5-15 bytes). Longer values fall
         * back to SIMD: memchr for the quote, taurus_text_contains
         * for '&' over the scanned prefix. pugixml's single-table
         * ct_parse_attr loop is the model; this is its shape with a
         * SIMD tail. */
        if (p->pos >= p->end) return -1;
        char quote = *p->pos;
        if (quote != '"' && quote != '\'') return -1;
        p->pos++;
        char* val_start = p->pos;
        int has_amp = 0;
        char* val_end = NULL;
        {
            const char* q = p->pos;
            const char* probe_end = (p->end - q > 48) ? q + 48 : p->end;
            while (q < probe_end) {
                char c = *q;
                if (c == quote) { val_end = (char*)q; goto value_done; }
                if (c == '&') has_amp = 1;
                q++;
            }
            if (q >= p->end) return -1; /* unterminated */
            val_end = (char*)memchr(q, quote, p->end - q);
            if (!val_end) return -1;
            if (q < val_end) {
                has_amp = has_amp ||
                          taurus_text_contains(q, (size_t)(val_end - q), '&');
            }
        }
    value_done:
        p->pos = val_end;
        *p->pos = '\0'; /* Safe: overwrites closing quote, not a delimiter */
        size_t val_len = p->pos - val_start;
        p->pos++; /* skip the NUL */

        /* Check for xmlns. */
        if (name_len >= 5 && name_start[0] == 'x' &&
            name_start[1] == 'm' && name_start[2] == 'l' &&
            name_start[3] == 'n' && name_start[4] == 's') {
            /* xmlns or xmlns:prefix. Name was NUL-terminated at
             * name_end above; value at val_start (closing quote
             * replaced with NUL). For "xmlns:foo" the prefix
             * portion is name_start+6 .. name_end; pool-alloc +
             * memcpy because we can't split the name in-place. */
            const char* ns_prefix = NULL;
            if (name_len > 6) {
                size_t plen = name_len - 6;
                ns_prefix = (char*)taurus_pool_alloc(p->pool, plen + 1);
                if (!ns_prefix) return -1;
                memcpy((void*)ns_prefix, name_start + 6, plen);
                ((char*)ns_prefix)[plen] = '\0';
            }
            struct taurus_namespace* ns =
                (struct taurus_namespace*)taurus_pool_alloc(
                    p->pool, sizeof(struct taurus_namespace));
            if (!ns) return -1;
            ns->prefix = (char*)ns_prefix;
            ns->uri = val_start; /* already NUL-terminated */
            ns->next = NULL;
            /* TODO 155 Phase A: parser has the pool directly; use it
             * to allocate ns_cache without going through the
             * (not-yet-registered) document. */
            struct taurus_namespace** head_ptr =
                taurus_elem_namespaces_ptr(elem, p->pool);
            if (!head_ptr) return -1;
            if (!*head_ptr) {
                *head_ptr = ns;
            } else {
                /* TODO 159 Phase G: use cached last-ns pointer for
                 * O(1) wiring instead of walking to the tail. */
                struct taurus_namespace* tail = p->current_elem_last_ns;
                if (tail) {
                    tail->next = ns;
                } else {
                    tail = *head_ptr;
                    while (tail->next) tail = tail->next;
                    tail->next = ns;
                }
            }
            p->current_elem_last_ns = ns;
            continue;
        }

        /* Regular attribute — zero-copy name/value, bulk-allocated struct. */
        if (dp_add_attr_inline(p, elem, name_start, name_len,
                                val_start, val_len, has_amp) != 0)
            return -1;
    }
    return -1;
}

/* Internal: parse from a writable, NUL-terminated buffer.
 * owns_buffer: 1 = document frees buf on taurus_document_free,
 *              0 = caller owns buf (in-place mode). */
/* owns_buffer values:
 *   0 — caller owns the buf (in-place parse). Don't free.
 *   1 — direct_parse_internal owns the buf via separate malloc.
 *       Free on failure.
 *   2 — buf should be copied into the pool (TODO 154 Phase C).
 *       The input `buf` is const; we pool-alloc a copy. Pool owns
 *       the copy. No separate free.
 */
static struct taurus_document* direct_parse_internal(char* buf, size_t len, int owns_buffer) {

    /* 2. Create arena-backed pool (TODO 183 Phase 3).
     *
     * Content-derived sizing: one cheap pre-scan counts '<' (an upper
     * bound on tags — close tags and '<' in text only over-count, which
     * over-allocates in the safe direction) and quote characters (each
     * attribute value contributes exactly 2, single- or double-quoted).
     * This sizes the element and attribute blocks to the document's
     * actual shape instead of a fixed len/10 heuristic — the K=100
     * many-attrs case (884 bytes/element) no longer over-allocates
     * attrs 5×, and text-heavy docs reserve room the old est missed.
     *
     * One arena replaces the old "single 4 MB-capped first pool page"
     * trick (#261): there is no page cap, so the bulk elem+attr block
     * and every text/string/mutation allocation share one contiguous
     * span. Exhaustion is a hard NULL → parse fails cleanly (pugixml
     * semantics) instead of scattering a second page megabytes away.
     *
     * Slack: len covers all text/name/value copies (they're substrings
     * of the document), plus len/2 mutation headroom (post-parse
     * append/set calls allocate from the same arena). */
    /* TODO 184: count3 — one memory pass for all three sizing
     * counters (was three full traversals; at 884 KB that's 1.7 MB
     * of avoided L2/DRAM traffic per parse). */
    size_t lt_count, dq_count, sq_count;
    taurus_text_count3(buf, len, '<', '"', '\'',
                       &lt_count, &dq_count, &sq_count);
    size_t quote_count = dq_count + sq_count;
    size_t est_elems = lt_count + 8;
    size_t elem_bytes = est_elems * sizeof(struct taurus_element);
    size_t attr_bytes = (quote_count / 2 + 64) * sizeof(struct taurus_attribute);
    /* len bounds all text/name/value content copies (substrings of the
     * document), but each text/comment NODE costs ~48-56 B of struct
     * overhead regardless of content length — one per element on
     * mixed-content docs, ~2x the per-item markup. Bound it per
     * element (CI catch: a 134 KB item-heavy doc exhausted the arena
     * at item 4144/5000 with 38 bytes left when this term was
     * missing). */
    size_t node_overhead = est_elems * 64;
    size_t text_room = len + node_overhead;
    size_t buf_extra = (owns_buffer == 2) ? (len + 1) : 0;
    size_t slack = len / 2 + 64 * 1024;  /* mutation headroom + floor */
    size_t arena_size = elem_bytes + attr_bytes + text_room + buf_extra + slack;
    TaurusArena* arena = taurus_arena_create(arena_size);
    if (!arena) {
        if (owns_buffer == 1) free(buf);
        return NULL;
    }
    TaurusMemoryPool* pool = taurus_pool_create_arena_backed(arena, 1);
    if (!pool) {
        taurus_arena_destroy(arena);
        if (owns_buffer == 1) free(buf);
        return NULL;
    }
    if (len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 128);
    }

    /* If owns_buffer == 2, copy the input into the pool now. The
     * caller's `buf` is const — we promised not to modify it. */
    if (owns_buffer == 2) {
        char* pool_buf = (char*)taurus_pool_alloc(pool, len + 1);
        if (!pool_buf) {
            taurus_pool_destroy(pool);
            return NULL;
        }
        memcpy(pool_buf, buf, len);
        pool_buf[len] = '\0';
        buf = pool_buf;
        owns_buffer = 0;  /* Pool owns the copy now. */
    }

    /* 3. Bulk-allocate element + attribute blocks as ONE contiguous
     * arena bump. Layout: [ elem_block | attr_block ]
     *
     * attr capacity follows the quote-derived estimate (each attr
     * value contributes exactly 2 quote chars). Overflow falls back
     * to per-attr pool alloc — which now bumps the same arena. */
    size_t attr_capacity = quote_count / 2 + 64;
    char* combined = (char*)taurus_pool_alloc(pool, elem_bytes + attr_bytes);
    if (!combined) {
        taurus_pool_destroy(pool);
        if (owns_buffer == 1) free(buf);
        return NULL;
    }
    TaurusElement elem_block = (TaurusElement)combined;
    memset(elem_block, 0, elem_bytes);
    struct taurus_attribute* attr_block =
        (struct taurus_attribute*)(combined + elem_bytes);
    /* No memset on attr_block — dp_add_attr_inline initializes every
     * field of each attr it uses. */
    size_t elem_idx = 0;

    /* 4. Create document. Pool-allocated to save one malloc per
     * parse (TODO 154 Phase B). The doc_pool_allocated flag tells
     * taurus_document_free to skip the TAURUS_FREE(doc) — pool
     * destroy reclaims it. */
    struct taurus_document* doc = (struct taurus_document*)
        taurus_pool_alloc(pool, sizeof(struct taurus_document));
    if (!doc) {
        taurus_pool_destroy(pool);
        if (owns_buffer == 1) free(buf);
        return NULL;
    }
    memset(doc, 0, sizeof(*doc));
    doc->doc_pool_allocated = 1;
    doc->strict_mode = g_taurus_strict_mode;
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);
    doc->ref_count = 1;
    doc->xml_buffer = buf;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = owns_buffer;
    /* No taurus_compact_set_current_document — direct_parse is
     * overflow-table-free. All compact pointer edges use direct
     * offset arithmetic, never touching the shared thread-local
     * overflow hash table. This eliminates cross-document
     * contamination under high document counts (#261). */

    /* 5. Parse. */
    DParser p;
    p.buf = buf;
    p.pos = buf;
    p.end = buf + len;
    p.pool = pool;
    p.doc = doc;
    p.depth = 0;
    p.root = NULL;
    p.pis_head = NULL;
    p.pis_tail = NULL;
    p.version = NULL;
    p.encoding = NULL;
    p.standalone = -1;
    p.had_declaration = 0;
    p.attr_block = attr_block;
    p.attr_idx = 0;
    p.attr_capacity = attr_capacity;
    p.dtd = NULL;
    p.line = 1;

    /* Skip BOM. */
    if (len >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        p.pos += 3;
    }

    while (p.pos < p.end) {
        /* Skip top-level whitespace. */
        if (p.depth == 0) {
            dp_skip_ws(&p);
            if (p.pos >= p.end) break;
        }

        if (*p.pos != '<') {
            /* Text content — FUSED scan (TODO 184): one inline loop
             * finds '<' AND counts '\n' (issue #223 line tracking),
             * replacing the memchr + dp_advance_line re-walk (two
             * passes; memchr pays ~10ns setup on short text — the
             * same finding as the attr-value scan). First 48 bytes
             * inline, SIMD fallback for longer spans. */
            char* text_start = p.pos;
            uint32_t line_snap = p.line;
            char* lt = NULL;
            {
                const char* q = p.pos;
                const char* probe_end =
                    (p.end - q > 48) ? q + 48 : p.end;
                while (q < probe_end) {
                    if (*q == '<') { lt = (char*)q; goto text_done; }
                    if (*q == '\n') p.line++;
                    q++;
                }
                if (q >= p.end) goto text_done;
                lt = (char*)memchr(q, '<', p.end - q);
                if (!lt) lt = p.end;
                /* Line count for the SIMD-skipped span only. */
                if (q < lt) {
                    p.line += (uint32_t)taurus_text_count_char(
                        q, (size_t)(lt - q), '\n');
                }
            }
        text_done:
            p.pos = lt ? lt : p.end;
            size_t tlen = p.pos - text_start;
            /* Snapshot line at the text start, then the loop above
             * already folded newlines — p.line is current. Issue #223. */
            uint32_t text_line = line_snap;
            if (tlen == 0) continue;
            if (p.depth == 0) {
                /* Whitespace-only between root and PIs. */
                for (char* c = text_start; c < p.pos; c++) {
                    if (!IS_WS(*c)) goto fail;
                }
                continue;
            }
            /* Text node. When a DTD is present (custom entity
             * declarations), eagerly expand entities into a pool-
             * allocated string — borrowed storage can't resolve
             * &foo; without the DTD at access time. Without a DTD,
             * stay borrowed and let taurus_text_get_content expand
             * predefined entities lazily. */
            TaurusTextNode* tn;
            if (p.dtd && tlen > 0 &&
                memchr(text_start, '&', tlen) != NULL) {
                TaurusStringView sv = taurus_sv_from_ptr(text_start, tlen);
                char* expanded = taurus_decode_entities_view_with_dtd(
                    &sv, p.dtd, pool);
                if (expanded) {
                    tn = taurus_text_create(expanded, strlen(expanded), pool);
                } else {
                    tn = taurus_text_create_borrowed(text_start, tlen, pool);
                }
            } else {
                tn = taurus_text_create_borrowed(text_start, tlen, pool);
            }
            if (!tn) goto fail;
            tn->base.line = text_line;
            dp_wire_child(&p, p.open_stack[p.depth - 1], (TaurusNode*)tn);
            continue;
        }

        /* '<' — dispatch. */
        if (p.end - p.pos < 2) goto fail;
        char next = p.pos[1];

        if (IS_NAME_START(next)) {
            /* Element. Snapshot line BEFORE scanning the open tag so
             * the element reports the line where '<' appeared. */
            uint32_t elem_line = p.line;
            TaurusElement elem;
            if (elem_idx < est_elems) {
                elem = &elem_block[elem_idx++];
            } else {
                /* Bulk block exhausted — fall back to pool_alloc.
                 * Pool pages are contiguous, so compact-pointer
                 * offsets remain valid as long as the pool's first
                 * page is large enough to hold both the bulk block
                 * and these overflow elements. direct_parse sizes
                 * the first page for exactly this (see page_size
                 * calculation above). */
                elem = (TaurusElement)taurus_pool_alloc(
                    pool, sizeof(struct taurus_element));
                if (!elem) goto fail;
                memset(elem, 0, sizeof(struct taurus_element));
            }
            elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
            elem->base.line = elem_line;
            /* TODO 155 Phase A: document field removed; root registered
             * below via taurus_root_doc_register. */

            /* Scan name (zero-copy). DON'T NUL-terminate yet — the
             * byte after the name is '>' or whitespace, which
             * dp_parse_attrs needs to see intact. We NUL-terminate
             * AFTER dp_parse_attrs returns. */
            p.pos++; /* skip '<' */
            char* name_start = p.pos;
            dp_scan_name(&p);
            size_t name_len = p.pos - name_start;

            /* Parse attributes (scans from the delimiter position). */
            int self_closing = dp_parse_attrs(&p, elem);
            if (self_closing < 0) goto fail;

            /* NOW safe to NUL-terminate the name — dp_parse_attrs
             * has finished scanning the open tag. */
            name_start[name_len] = '\0';

            /* Split prefix:local. */
            char* colon = strchr(name_start, ':');
            if (colon) {
                *colon = '\0';
                taurus_elem_set_prefix(elem, name_start, p.pool);
                elem->name = colon + 1;
            } else {
                elem->name = name_start;
            }

            /* Compute 16-bit FNV-1a hash of local name for fast
             * child-axis lookups in XPath (TODO 159). Compares
             * 2 bytes before falling back to strcmp. */
            {
                uint16_t h = 0x811C;
                for (const char* c = elem->name; *c; c++) {
                    h ^= (unsigned char)*c;
                    h *= 0x0193;
                }
                elem->name_hash = h;
            }

            /* Wire into parent. */
            if (p.depth > 0) {
                dp_wire_child(&p, p.open_stack[p.depth - 1], (TaurusNode*)elem);
            } else {
                if (p.root) goto fail; /* multiple top-level elements */
                p.root = elem;
            }

            if (!self_closing) {
                /* Respect custom depth limit (g_taurus_max_depth)
                 * when set by the caller; fall back to the
                 * compile-time default otherwise. */
                extern TAURUS_THREAD_LOCAL int g_taurus_max_depth;
                int max_depth = g_taurus_max_depth > 0
                    ? g_taurus_max_depth : DP_MAX_DEPTH;
                if (p.depth >= max_depth) goto fail;
                p.open_stack[p.depth++] = elem;
                /* Initialize last_child_stack for the new depth. */
                p.last_child_stack[p.depth - 1] = NULL;
            }
        }
        else if (next == '/') {
            /* Close tag. */
            p.pos += 2;
            /* Scan name (includes prefix:local). */
            char* close_start = p.pos;
            dp_scan_name(&p);
            size_t close_len = p.pos - close_start;
            dp_skip_ws(&p);
            if (p.pos >= p.end || *p.pos != '>') goto fail;
            p.pos++;
            if (p.depth == 0) goto fail;
            /* Verify close tag matches open element. The open
             * element's name is the LOCAL part (prefix stripped).
             * Compare the local part of the close tag, which is
             * everything after ':' (or the whole name if no ':'). */
            TaurusElement open = p.open_stack[p.depth - 1];
            const char* open_name = open->name;
            size_t open_len = strlen(open_name);
            const char* close_local = close_start;
            size_t close_local_len = close_len;
            const char* colon = (const char*)memchr(close_start, ':', close_len);
            if (colon) {
                close_local = colon + 1;
                close_local_len = close_len - (colon + 1 - close_start);
            }
            if (open_len != close_local_len ||
                memcmp(open_name, close_local, close_local_len) != 0)
                goto fail;
            p.depth--;
        }
        else if (next == '!') {
            /* Comment, CDATA, or DOCTYPE. */
            uint32_t markup_line = p.line;
            if (p.end - p.pos >= 4 && p.pos[2] == '-' && p.pos[3] == '-') {
                /* Comment: <!-- ... --> */
                p.pos += 4;
                char* start = p.pos;
                /* TODO 177: SIMD find3 for "-->" (NEON/AVX2 when
                 * available, scalar memchr-anchor fallback). Replaces
                 * the per-candidate dash verify loop; dash-run-heavy
                 * comment bodies no longer re-scan candidates. */
                ptrdiff_t hit = taurus_text_find3(
                    p.pos, (size_t)(p.end - p.pos), '-', '-', '>');
                if (hit < 0) goto fail;
                p.pos += hit;
                *p.pos = '\0'; /* NUL-terminate content */
                TaurusCommentNode* cn = taurus_comment_create(
                    start, p.pos - start, pool);
                if (!cn) goto fail;
                cn->base.line = markup_line;
                if (p.depth > 0) {
                    dp_wire_child(&p, p.open_stack[p.depth - 1], (TaurusNode*)cn);
                }
                p.pos += 3;
                dp_advance_line(&p, start, p.pos);
            } else if (p.end - p.pos >= 9 &&
                       memcmp(p.pos + 2, "[CDATA[", 7) == 0) {
                /* CDATA: <![CDATA[ ... ]]> */
                p.pos += 9;
                char* start = p.pos;
                /* TODO 177: SIMD find3 for "]]>". */
                ptrdiff_t hit = taurus_text_find3(
                    p.pos, (size_t)(p.end - p.pos), ']', ']', '>');
                if (hit < 0) goto fail;
                p.pos += hit;
                *p.pos = '\0';
                TaurusCDATANode* cd = taurus_cdata_create(
                    start, p.pos - start, pool);
                if (!cd) goto fail;
                cd->base.line = markup_line;
                if (p.depth > 0) {
                    dp_wire_child(&p, p.open_stack[p.depth - 1], (TaurusNode*)cd);
                }
                p.pos += 3;
                dp_advance_line(&p, start, p.pos);
            } else {
                /* DOCTYPE: extract name + internal subset. Cold path
                 * extracted to dp_parse_doctype (TODO 166 Phase A). */
                if (dp_parse_doctype(&p, markup_line) != 0) goto fail;
                continue;
            }
        }
        else if (next == '?') {
            /* PI: <?target data?> or XML declaration. */
            uint32_t pi_line = p.line;
            char* pi_start = p.pos;
            p.pos += 2;
            char* target_start = p.pos;
            if (!IS_NAME_START(*p.pos)) goto fail;
            p.pos++;
            dp_scan_name(&p);
            *p.pos = '\0';
            p.pos++;
            char* data_start = p.pos;
            /* memchr for '?' — fast-skip PI data bodies. */
            {
                char* q = (char*)memchr(p.pos, '?', p.end - p.pos);
                if (!q) goto fail;
                p.pos = q;
            }
            if (p.pos + 1 >= p.end || p.pos[1] != '>') goto fail;
            *p.pos = '\0'; /* NUL-terminate data */
            size_t data_len = p.pos - data_start;
            p.pos += 2;
            dp_advance_line(&p, pi_start, p.pos);

            /* XML declaration handling. */
            if (strcmp(target_start, "xml") == 0) {
                p.had_declaration = 1;
                /* Parse version/encoding/standalone from data. Values
                 * are NUL-terminated in-place by overwriting the
                 * closing quote; the final heap-strdup is required
                 * because taurus_document_free calls TAURUS_FREE on
                 * these fields. */
                char* scan = data_start;
                while (*scan) {
                    while (IS_WS(*scan)) scan++;
                    if (!*scan) break;
                    char* pname = scan;
                    while (*scan && !IS_WS(*scan) && *scan != '=') scan++;
                    size_t pname_len = scan - pname;
                    while (IS_WS(*scan)) scan++;
                    if (*scan != '=') break;
                    scan++;
                    while (IS_WS(*scan)) scan++;
                    if (*scan != '"' && *scan != '\'') break;
                    char q = *scan++;
                    char* pval = scan;
                    while (*scan && *scan != q) scan++;
                    if (*scan != q) break;
                    *scan = '\0';
                    scan++;

                    if (pname_len == 7 && memcmp(pname, "version", 7) == 0) {
                        p.version = pval;
                    } else if (pname_len == 8 && memcmp(pname, "encoding", 8) == 0) {
                        p.encoding = pval;
                    } else if (pname_len == 10 && memcmp(pname, "standalone", 10) == 0) {
                        if (strcmp(pval, "yes") == 0) p.standalone = 1;
                        else if (strcmp(pval, "no") == 0) p.standalone = 0;
                    }
                }
                continue;
            }

            TaurusPINode* pi = taurus_pi_create(
                target_start, strlen(target_start),
                data_start, data_len, pool);
            if (!pi) goto fail;
            pi->base.line = pi_line;
            if (p.depth > 0) {
                dp_wire_child(&p, p.open_stack[p.depth - 1], (TaurusNode*)pi);
            } else {
                /* Doc-level PI. */
                struct taurus_processing_instruction* pi_node =
                    (struct taurus_processing_instruction*)malloc(sizeof(*pi_node));
                if (!pi_node) goto fail;
                pi_node->target = strdup(target_start);
                pi_node->data = strdup(data_start);
                pi_node->next = NULL;
                if (p.pis_tail) p.pis_tail->next = pi_node;
                else p.pis_head = pi_node;
                p.pis_tail = pi_node;
            }
        }
        else {
            goto fail;
        }
    }

    if (p.depth != 0) goto fail;
    if (!p.root) goto fail;

    /* 6. Commit to doc. */
    doc->new_dom_root = p.root;
    /* TODO 155 Phase A: register root→doc mapping in the thread-local
     * hash so non-root elements can reach the doc via walk + lookup. */
    taurus_root_doc_register(p.root, doc);
    /* Tree is eagerly built — no FlatDoc, no lazy promote. */
    doc->pis = p.pis_head;
    doc->dtd = p.dtd;  /* NULL when no DOCTYPE internal subset */
    doc->had_declaration = p.had_declaration;
    doc->standalone = p.standalone;
    /* encoding and xml_version are borrowed pointers into doc->xml_buffer.
     * taurus_document_free calls TAURUS_FREE on them, so we must heap-strdup. */
    if (p.encoding) {
        doc->encoding = strdup(p.encoding);
        if (!doc->encoding) goto fail;
    }
    if (p.version) {
        doc->xml_version = strdup(p.version);
        if (!doc->xml_version) goto fail;
    }

    /* 7. Freeze tree (match legacy parser behavior). */
    taurus_document_freeze_tree(doc);

    {
        static int dbg_ok = -1;
        if (dbg_ok < 0) dbg_ok = getenv("TAURUS_DEBUG_PARSE") != NULL;
        if (dbg_ok) {
            fprintf(stderr, "dp ok: arena_used=%zu arena_cap=%zu elem_idx=%zu attr_idx=%zu\n",
                    arena->used, arena->size, elem_idx, p.attr_idx);
        }
    }

    return doc;

fail:
    {
        /* Cached env lookup — getenv is a linear environ scan, too
         * costly to repeat on every parse. Benign init race. */
        static int dbg = -1;
        if (dbg < 0) dbg = getenv("TAURUS_DEBUG_PARSE") != NULL;
        if (dbg) {
            fprintf(stderr, "dp fail: pos=%ld off=%ld depth=%d arena_used=%zu arena_cap=%zu elem_idx=%zu attr_idx=%zu lt=%zu q=%zu est=%zu attrcap=%zu\n",
                    (long)(p.pos - p.buf), (long)(p.end - p.pos), p.depth,
                    arena ? arena->used : 0, arena ? arena->size : 0,
                    elem_idx, p.attr_idx, lt_count, quote_count, est_elems,
                    attr_capacity);
        }
    }
    taurus_pool_destroy(pool);
    /* elem_block AND doc are pool-allocated — both freed by
     * pool_destroy above. Don't TAURUS_FREE(doc) (TODO 154). */
    if (owns_buffer == 1) free(buf);  /* Only free our own copy, not caller's */
    return NULL;
}

/* Public: copy the input then parse (standard path).
 *
 * The input copy is allocated FROM THE POOL (not via malloc) so the
 * document owns exactly one allocation that's freed in one shot at
 * taurus_document_free. Saves one malloc+free per parse call vs the
 * old pattern of malloc(buf) + pool_alloc(everything else).
 *
 * TODO 154 Phase C. */
struct taurus_document* direct_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;
    /* The pool-allocate-then-parse path lives in
     * direct_parse_internal. owns_buffer=2 signals "copy the input
     * into the pool, then parse." */
    return direct_parse_internal((char*)xml, len, 2);
}

/* Public: parse a caller-owned writable buffer without copying. */
struct taurus_document* direct_parse_inplace(char* buf, size_t len) {
    if (!buf || len == 0) return NULL;
    buf[len] = '\0';  /* Ensure NUL termination */
    return direct_parse_internal(buf, len, 0);
}
