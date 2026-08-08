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
#include "../common/string_view.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern __thread int g_taurus_strict_mode;
extern void taurus_compact_set_current_document(struct taurus_document* doc);
int taurus_element_add_namespace(struct taurus_element* elem,
                                  struct taurus_namespace* ns);

/* Lookup tables (shared with flat_parser.c — same definitions). */
static const uint8_t dp_name_char_lut[256] = {
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,['w']=1,['x']=1,
    ['y']=1,['z']=1,
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,['W']=1,['X']=1,
    ['Y']=1,['Z']=1,
    ['0']=1,['1']=1,['2']=1,['3']=1,['4']=1,['5']=1,['6']=1,['7']=1,
    ['8']=1,['9']=1,
    ['_']=1,[':']=1,['-']=1,['.']=1,
};
static const uint8_t dp_name_start_lut[256] = {
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,['w']=1,['x']=1,
    ['y']=1,['z']=1,
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,['W']=1,['X']=1,
    ['Y']=1,['Z']=1,
    ['_']=1,[':']=1,
};
static const uint8_t dp_ws_lut[256] = {
    [' ']=1,['\t']=1,['\n']=1,['\r']=1,
};

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
    int had_declaration;
    /* Bulk-allocated attribute block. Pre-allocated from pool so the
     * common case is a bump-pointer off the block — no per-attr
     * pool_alloc, no name interning, no value pool_strdup. Overflow
     * falls back to per-attr pool_alloc. */
    struct taurus_attribute* attr_block;
    size_t attr_idx;
    size_t attr_capacity;
} DParser;

static inline void dp_skip_ws(DParser* p) {
    while (p->pos < p->end && dp_ws_lut[(unsigned char)*p->pos]) p->pos++;
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
    attr->value = val;            /* zero-copy, NUL-terminated in buffer */
    attr->prefix = NULL;
    attr->namespace_uri = NULL;
    attr->next = NULL;
    attr->has_entities = 0;       /* direct_parse excludes entity inputs */

    /* FNV-1a hash inline — used by attribute-index lookups. */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619u;
    }
    attr->name_hash = h;

    /* Append via cached last_attribute offset (TODO 106).
     * Direct offset write — attr and elem share the pre-warmed pool
     * page, so offsets are within int32 range and we can skip the
     * overflow-table encode/decode call path. Same trick as
     * dp_wire_child for tree edges. */
    if (elem->last_attribute_off != 0) {
        struct taurus_attribute* last =
            (struct taurus_attribute*)((char*)elem + elem->last_attribute_off);
        last->next = attr;
    } else {
        elem->first_attribute_off = (int32_t)((char*)attr - (char*)elem);
    }
    elem->last_attribute_off = (int32_t)((char*)attr - (char*)elem);
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
        if (!dp_name_start_lut[(unsigned char)*p->pos]) return -1;
        p->pos++;
        while (p->pos < p->end && dp_name_char_lut[(unsigned char)*p->pos])
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
        while (p->pos < p->end && *p->pos != quote) p->pos++;
        if (p->pos >= p->end) return -1;
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

    /* Skip BOM. */
    if (len >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        p.pos += 3;
    }

    int root_seen = 0;

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
            if (tlen == 0) continue;
            if (p.depth == 0) {
                /* Whitespace-only between root and PIs. */
                for (char* c = text_start; c < p.pos; c++) {
                    if (!dp_ws_lut[(unsigned char)*c]) goto fail;
                }
                continue;
            }
            /* Text node: borrowed pointer + length. NOT NUL-terminated
             * here — taurus_text_get_content lazily materializes on
             * first access. Writing NUL would corrupt the '<' the
             * scanner needs to see next iteration. */
            TaurusTextNode* tn = taurus_text_create_borrowed(
                text_start, tlen, pool);
            if (!tn) goto fail;
            dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)tn);
            continue;
        }

        /* '<' — dispatch. */
        if (p.end - p.pos < 2) goto fail;
        char next = p.pos[1];

        if (dp_name_start_lut[(unsigned char)next]) {
            /* Element. */
            if (elem_idx >= est_elems) {
                /* Grow: realloc + adjust offsets. Complex; for now fail. */
                goto fail;
            }
            TaurusElement elem = &elem_block[elem_idx++];
            elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
            elem->document = doc;

            /* Scan name (zero-copy). DON'T NUL-terminate yet — the
             * byte after the name is '>' or whitespace, which
             * dp_parse_attrs needs to see intact. We NUL-terminate
             * AFTER dp_parse_attrs returns. */
            p.pos++; /* skip '<' */
            char* name_start = p.pos;
            while (p.pos < p.end && dp_name_char_lut[(unsigned char)*p.pos])
                p.pos++;
            size_t name_len = p.pos - name_start;
            char name_delim = *p.pos; /* save delimiter byte */

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
                elem->prefix = name_start;
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
                if (p.depth >= DP_MAX_DEPTH) goto fail;
                p.open_stack[p.depth++] = elem;
            }
            root_seen = 1;
        }
        else if (next == '/') {
            /* Close tag. */
            p.pos += 2;
            /* Scan name. */
            char* close_start = p.pos;
            while (p.pos < p.end && dp_name_char_lut[(unsigned char)*p.pos])
                p.pos++;
            size_t close_len = p.pos - close_start;
            dp_skip_ws(&p);
            if (p.pos >= p.end || *p.pos != '>') goto fail;
            p.pos++;
            if (p.depth == 0) goto fail;
            /* Verify close tag name matches open element. */
            TaurusElement open = p.open_stack[p.depth - 1];
            const char* open_name = open->name;
            size_t open_len = strlen(open_name);
            if (open_len != close_len ||
                memcmp(open_name, close_start, close_len) != 0)
                goto fail;
            p.depth--;
        }
        else if (next == '!') {
            /* Comment, CDATA, or DOCTYPE. */
            if (p.end - p.pos >= 4 && p.pos[2] == '-' && p.pos[3] == '-') {
                /* Comment: <!-- ... --> */
                p.pos += 4;
                char* start = p.pos;
                while (p.pos + 2 < p.end) {
                    if (p.pos[0] == '-' && p.pos[1] == '-' && p.pos[2] == '>') {
                        *p.pos = '\0'; /* NUL-terminate content */
                        TaurusCommentNode* cn = taurus_comment_create(
                            start, p.pos - start, pool);
                        if (!cn) goto fail;
                        if (p.depth > 0) {
                            dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)cn);
                        }
                        p.pos += 3;
                        break;
                    }
                    p.pos++;
                }
            } else if (p.end - p.pos >= 9 &&
                       memcmp(p.pos + 2, "[CDATA[", 7) == 0) {
                /* CDATA: <![CDATA[ ... ]]> */
                p.pos += 9;
                char* start = p.pos;
                while (p.pos + 2 < p.end) {
                    if (p.pos[0] == ']' && p.pos[1] == ']' && p.pos[2] == '>') {
                        *p.pos = '\0';
                        TaurusCDATANode* cd = taurus_cdata_create(
                            start, p.pos - start, pool);
                        if (!cd) goto fail;
                        if (p.depth > 0) {
                            dp_wire_child(p.open_stack[p.depth - 1], (TaurusNode*)cd);
                        }
                        p.pos += 3;
                        break;
                    }
                    p.pos++;
                }
            } else {
                /* DOCTYPE: skip to matching '>' with bracket counting. */
                p.pos += 2;
                int bd = 0;
                while (p.pos < p.end) {
                    char c = *p.pos++;
                    if (c == '[') bd++;
                    else if (c == ']') { if (bd > 0) bd--; }
                    else if (c == '>' && bd == 0) break;
                }
            }
        }
        else if (next == '?') {
            /* PI: <?target data?> or XML declaration. */
            p.pos += 2;
            char* target_start = p.pos;
            if (!dp_name_start_lut[(unsigned char)*p.pos]) goto fail;
            p.pos++;
            while (p.pos < p.end && dp_name_char_lut[(unsigned char)*p.pos])
                p.pos++;
            *p.pos = '\0';
            p.pos++;
            char* data_start = p.pos;
            while (p.pos < p.end && *p.pos != '?') p.pos++;
            if (p.pos + 1 >= p.end || p.pos[1] != '>') goto fail;
            *p.pos = '\0'; /* NUL-terminate data */
            size_t data_len = p.pos - data_start;
            p.pos += 2;

            /* XML declaration handling. */
            if (strcmp(target_start, "xml") == 0) {
                p.had_declaration = 1;
                /* Parse version/encoding/standandalone from data. */
                /* For simplicity, just note we saw it. */
                continue;
            }

            TaurusPINode* pi = taurus_pi_create(
                target_start, strlen(target_start),
                data_start, data_len, pool);
            if (!pi) goto fail;
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
    doc->flat_promoted = 1; /* No FlatDoc, tree is ready */
    doc->pis = p.pis_head;
    doc->had_declaration = p.had_declaration;
    doc->standalone = -1;

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
