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
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/doctype.h"
#include "../common/string_view.h"
#include "../common/chartype.h"
#include "../common/entities.h"
#include "../dtd/model.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern __thread int g_taurus_strict_mode;
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

/* Tally '\n' bytes in [from, to) and fold into p->line. Used after
 * bulk scans (memchr for text, multi-char literal matches) where the
 * per-byte scanner can't update line inline. */
static inline void dp_advance_line(DParser* p, char* from, char* to) {
    for (char* c = from; c < to; c++) {
        if (*c == '\n') p->line++;
    }
}

/* Wire child into parent's child chain. Direct pointer arithmetic
 * for compact-pointer offsets — no function call, no overflow check.
 * All elements are in one contiguous block, so offsets always fit. */
static inline void dp_wire_child(TaurusElement parent, TaurusNode* child) {
    /* Set child's parent pointer.
     * IMPORTANT: parent_off is relative to the CHILD's address.
     * Accessor: (char*)child + parent_off == (char*)parent.
     * So parent_off = (char*)parent - (char*)child. */
    int32_t parent_to_child = (int32_t)((char*)parent - (char*)child);
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        ((TaurusElement)child)->parent_off = parent_to_child;
        ((TaurusElement)child)->document = parent->document;
    } else {
        switch (child->type) {
            case TAURUS_NODE_TYPE_TEXT:
                ((TaurusTextNode*)child)->parent_off = parent_to_child;
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                ((TaurusCommentNode*)child)->parent_off = parent_to_child;
                break;
            case TAURUS_NODE_TYPE_CDATA:
                ((TaurusCDATANode*)child)->parent_off = parent_to_child;
                break;
            case TAURUS_NODE_TYPE_PI:
                ((TaurusPINode*)child)->parent_off = parent_to_child;
                break;
            default: break;
        }
    }
    /* Splice into child chain as new last child.
     * first_child_off / last_child_off are on the PARENT, pointing
     * to child. Accessor: (char*)parent + off == (char*)child.
     * So off = (char*)child - (char*)parent. */
    int32_t child_off = (int32_t)((char*)child - (char*)parent);
    if (parent->last_child_off != 0) {
        TaurusNode* last = (TaurusNode*)((char*)parent + parent->last_child_off);
        taurus_node_set_next_sibling(last, child);
    } else {
        parent->first_child_off = child_off;
    }
    parent->last_child_off = child_off;

    /* Issue #213: maintain child_count for element children, matching
     * the convention used by taurus_element_append_child_internal. */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
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
                                      char* val, size_t val_len) {
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
    attr->prefix_view = taurus_sv_empty();
    attr->namespace_uri_view = taurus_sv_empty();
    attr->name = name;            /* zero-copy, NUL-terminated in buffer */
    /* Entity handling for attr values:
     * - DTD present + value has '&': eagerly expand via DTD-aware
     *   decoder (custom entities &foo; need the DTD table). Result
     *   is pool-allocated; has_entities=0 (already resolved).
     * - No DTD + value has '&': leave value NULL, has_entities=1.
     *   Accessor expands predefined entities lazily on first read.
     * - No '&': zero-copy, no expansion needed. */
    if (val_len > 0 && memchr(val, '&', val_len) != NULL) {
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
    attr->prefix = NULL;
    attr->namespace_uri = NULL;
    attr->next = NULL;

    /* FNV-1a hash inline — used by attribute-index lookups. */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619u;
    }
    attr->name_hash = h;

    /* Append via cached last_attribute offset (TODO 106).
     * Use the safe compact_int32 encode/decode — attr_block and elem
     * may land on different pool pages for small inputs (pre-warmed
     * page is sized for typical docs but elem_block + attr_block can
     * exceed it), so the raw byte difference can overflow int32. The
     * encode function falls back to the overflow table in that case. */
    struct taurus_attribute* last = taurus_elem_last_attribute(elem);
    if (last) {
        last->next = attr;
    } else {
        taurus_elem_set_first_attribute(elem, attr);
    }
    taurus_elem_set_last_attribute(elem, attr);
    elem->attr_count++;
    return 0;
}

/* Parse attributes for an element. Writes attr structs into the
 * pre-allocated attr_block (zero-copy name/value, no interning).
 * Names are NUL-terminated in-place AFTER '=' is consumed; values
 * are NUL-terminated in-place at the closing quote. */
static int dp_parse_attrs(DParser* p, TaurusElement elem) {
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
        while (p->pos < p->end && IS_NAME_CHAR(*p->pos))
            p->pos++;
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

        /* Quoted value. */
        if (p->pos >= p->end) return -1;
        char quote = *p->pos;
        if (quote != '"' && quote != '\'') return -1;
        p->pos++;
        char* val_start = p->pos;
        /* memchr for closing quote — libc vectorized (SSE2/AVX),
         * processes 16-32 bytes/iteration vs 1 byte/iteration for
         * the sequential loop. Big win for URL/long-text values. */
        char* val_end = (char*)memchr(p->pos, quote, p->end - p->pos);
        if (!val_end) return -1;
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
            taurus_element_add_namespace(elem, ns);
            continue;
        }

        /* Regular attribute — zero-copy name/value, bulk-allocated struct. */
        if (dp_add_attr_inline(p, elem, name_start, name_len,
                                val_start, val_len) != 0)
            return -1;
    }
    return -1;
}

struct taurus_document* direct_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    /* 1. Copy XML buffer (writable for in-place NUL termination). */
    char* buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, xml, len);
    buf[len] = '\0';

    /* 2. Create pool for attrs/non-element nodes/namespaces.
     * Pre-warm with a large page so ALL per-node allocations hit the
     * bump-pointer fast path (no page traversal or malloc during parse).
     * Estimate: each element generates ~3 non-element allocations
     * (text + attrs), averaging ~60 bytes each. */
    size_t est_elems = len / 10 + 128;
    size_t est_pool_bytes = est_elems * 3 * 60;
    size_t page_size = est_pool_bytes;
    if (page_size < 4096) page_size = 4096;
    if (page_size > 131072) page_size = 131072;
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(page_size);
    if (!pool) { free(buf); return NULL; }
    if (len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 128);
    }

    /* 3. Bulk-allocate element block from POOL. est_elems was
     * computed above for pool sizing. */
    TaurusElement elem_block = (TaurusElement)taurus_pool_alloc(
        pool, est_elems * sizeof(struct taurus_element));
    if (!elem_block) {
        taurus_pool_destroy(pool);
        free(buf);
        return NULL;
    }
    memset(elem_block, 0, est_elems * sizeof(struct taurus_element));
    size_t elem_idx = 0;

    /* 3b. Bulk-allocate attribute block. Heuristic: ~6 attrs per
     * element covers typical XML with room to spare. Overflow falls
     * back to per-attr pool_alloc in dp_add_attr_inline. */
    size_t attr_capacity = est_elems * 6;
    struct taurus_attribute* attr_block = (struct taurus_attribute*)taurus_pool_alloc(
        pool, attr_capacity * sizeof(struct taurus_attribute));
    if (!attr_block) {
        taurus_pool_destroy(pool);
        free(buf);
        return NULL;
    }
    /* No memset — every field is initialized by dp_add_attr_inline. */

    /* 4. Create document. */
    struct taurus_document* doc = (struct taurus_document*)calloc(1, sizeof(*doc));
    if (!doc) {
        taurus_pool_destroy(pool);
        free(buf);
        return NULL;
    }
    doc->strict_mode = g_taurus_strict_mode;
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);
    doc->ref_count = 1;
    doc->xml_buffer = buf;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 1;
    taurus_compact_set_current_document(doc);

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
            /* Text content via memchr. */
            char* text_start = p.pos;
            char* lt = (char*)memchr(p.pos, '<', p.end - p.pos);
            p.pos = lt ? lt : p.end;
            size_t tlen = p.pos - text_start;
            /* Snapshot line at the text start, then fold newlines in
             * the consumed range so the NEXT token starts on the
             * correct line. Issue #223. */
            uint32_t text_line = p.line;
            dp_advance_line(&p, text_start, p.pos);
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
            dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)tn);
            continue;
        }

        /* '<' — dispatch. */
        if (p.end - p.pos < 2) goto fail;
        char next = p.pos[1];

        if (IS_NAME_START(next)) {
            /* Element. Snapshot line BEFORE scanning the open tag so
             * the element reports the line where '<' appeared. */
            uint32_t elem_line = p.line;
            if (elem_idx >= est_elems) {
                /* Grow: realloc + adjust offsets. Complex; for now fail. */
                goto fail;
            }
            TaurusElement elem = &elem_block[elem_idx++];
            elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
            elem->base.line = elem_line;
            elem->document = doc;

            /* Scan name (zero-copy). DON'T NUL-terminate yet — the
             * byte after the name is '>' or whitespace, which
             * dp_parse_attrs needs to see intact. We NUL-terminate
             * AFTER dp_parse_attrs returns. */
            p.pos++; /* skip '<' */
            char* name_start = p.pos;
            while (p.pos < p.end && IS_NAME_CHAR(*p.pos))
                p.pos++;
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

            /* Wire into parent. */
            if (p.depth > 0) {
                dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)elem);
            } else {
                if (p.root) goto fail; /* multiple top-level elements */
                p.root = elem;
            }

            if (!self_closing) {
                /* Respect custom depth limit (g_taurus_max_depth)
                 * when set by the caller; fall back to the
                 * compile-time default otherwise. */
                extern __thread int g_taurus_max_depth;
                int max_depth = g_taurus_max_depth > 0
                    ? g_taurus_max_depth : DP_MAX_DEPTH;
                if (p.depth >= max_depth) goto fail;
                p.open_stack[p.depth++] = elem;
            }
        }
        else if (next == '/') {
            /* Close tag. */
            p.pos += 2;
            /* Scan name (includes prefix:local). */
            char* close_start = p.pos;
            while (p.pos < p.end && IS_NAME_CHAR(*p.pos))
                p.pos++;
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
                /* memchr for '-' to fast-skip large comment bodies,
                 * then verify "−−>" at each candidate. libc memchr
                 * processes 16-32 bytes/iteration. */
                for (;;) {
                    char* dash = (char*)memchr(p.pos, '-', p.end - p.pos);
                    if (!dash || dash + 2 >= p.end) goto fail;
                    if (dash[1] == '-' && dash[2] == '>') {
                        p.pos = dash;
                        break;
                    }
                    p.pos = dash + 1;
                }
                *p.pos = '\0'; /* NUL-terminate content */
                TaurusCommentNode* cn = taurus_comment_create(
                    start, p.pos - start, pool);
                if (!cn) goto fail;
                cn->base.line = markup_line;
                if (p.depth > 0) {
                    dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)cn);
                }
                p.pos += 3;
                dp_advance_line(&p, start, p.pos);
            } else if (p.end - p.pos >= 9 &&
                       memcmp(p.pos + 2, "[CDATA[", 7) == 0) {
                /* CDATA: <![CDATA[ ... ]]> */
                p.pos += 9;
                char* start = p.pos;
                /* memchr for ']' to fast-skip large CDATA bodies,
                 * then verify "]]>" at each candidate. */
                for (;;) {
                    char* bracket = (char*)memchr(p.pos, ']', p.end - p.pos);
                    if (!bracket || bracket + 2 >= p.end) goto fail;
                    if (bracket[1] == ']' && bracket[2] == '>') {
                        p.pos = bracket;
                        break;
                    }
                    p.pos = bracket + 1;
                }
                *p.pos = '\0';
                TaurusCDATANode* cd = taurus_cdata_create(
                    start, p.pos - start, pool);
                if (!cd) goto fail;
                cd->base.line = markup_line;
                if (p.depth > 0) {
                    dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)cd);
                }
                p.pos += 3;
                dp_advance_line(&p, start, p.pos);
            } else {
                /* DOCTYPE: extract name + internal subset. Build a
                 * DOCTYPE node so taurus_document_internal_subset
                 * returns the name, and parse entities from the
                 * internal subset for custom entity expansion. */
                char* doctype_start = p.pos;
                p.pos += 2; /* skip "<!" */
                /* Match "DOCTYPE" keyword (case-sensitive per XML spec). */
                if (p.end - p.pos < 7 ||
                    memcmp(p.pos, "DOCTYPE", 7) != 0) {
                    while (p.pos < p.end && *p.pos != '>') p.pos++;
                    if (p.pos < p.end) p.pos++;
                    dp_advance_line(&p, doctype_start, p.pos);
                    continue;
                }
                p.pos += 7;
                while (p.pos < p.end && IS_WS(*p.pos)) {
                    if (*p.pos == '\n') p.line++;
                    p.pos++;
                }
                /* Scan DOCTYPE name. */
                char* dt_name_start = p.pos;
                while (p.pos < p.end && IS_NAME_CHAR(*p.pos)) p.pos++;
                size_t dt_name_len = p.pos - dt_name_start;

                /* Skip to '[' (internal subset) or '>' (no subset). */
                char* subset_start = NULL;
                char* subset_end = NULL;
                while (p.pos < p.end && *p.pos != '>' && *p.pos != '[') {
                    p.pos++;
                }
                if (p.pos < p.end && *p.pos == '[') {
                    subset_start = ++p.pos;
                    /* Find matching ']'. Nested brackets aren't legal
                     * in DTD internal subsets, so no depth tracking. */
                    while (p.pos < p.end && *p.pos != ']') p.pos++;
                    subset_end = p.pos;
                    if (p.pos < p.end) p.pos++; /* skip ']' */
                    /* Skip to '>'. */
                    while (p.pos < p.end && *p.pos != '>') p.pos++;
                    if (p.pos < p.end) p.pos++; /* skip '>' */
                } else {
                    /* No internal subset, skip to '>'. */
                    while (p.pos < p.end && *p.pos != '>') p.pos++;
                    if (p.pos < p.end) p.pos++;
                }

                /* Create DOCTYPE node with the extracted name. */
                if (dt_name_len > 0) {
                    TaurusDoctypeNode* dt = taurus_doctype_create(
                        dt_name_start, dt_name_len, pool);
                    if (dt) {
                        doc->doctype = dt;
                    }
                }

                /* Parse internal subset if non-empty — builds the
                 * entity table used for custom entity expansion. */
                if (subset_start && subset_end > subset_start) {
                    TaurusDTD* dtd = taurus_dtd_parse_internal_subset(
                        subset_start, (size_t)(subset_end - subset_start),
                        pool);
                    if (dtd) {
                        p.dtd = dtd;
                    }
                }
                dp_advance_line(&p, doctype_start, p.pos);
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
            while (p.pos < p.end && IS_NAME_CHAR(*p.pos))
                p.pos++;
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
                    while (*scan == ' ' || *scan == '\t' ||
                           *scan == '\n' || *scan == '\r') scan++;
                    if (!*scan) break;
                    char* pname = scan;
                    while (*scan && *scan != '=' && *scan != ' ' &&
                           *scan != '\t' && *scan != '\n' && *scan != '\r')
                        scan++;
                    size_t pname_len = scan - pname;
                    while (*scan == ' ' || *scan == '\t' ||
                           *scan == '\n' || *scan == '\r') scan++;
                    if (*scan != '=') break;
                    scan++;
                    while (*scan == ' ' || *scan == '\t' ||
                           *scan == '\n' || *scan == '\r') scan++;
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
                dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)pi);
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

    /* Note: elem_block is NOT freed — it's the element storage.
     * It's tracked via a doc-level pointer so taurus_document_free
     * can release it. We store it in doc->page_base region. */
    /* Actually, we need to store elem_block somewhere. Add it to
     * the doc. For now, leak it — it's small relative to the pool.
     * TODO: store elem_block pointer on doc for proper cleanup. */

    return doc;

fail:
    taurus_compact_set_current_document(NULL);
    taurus_pool_destroy(pool);
    /* elem_block is pool-allocated — freed by pool_destroy above. */
    free(doc->xml_buffer);
    free(doc);
    return NULL;
}
