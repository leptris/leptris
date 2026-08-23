/* lib/src/serialize/serialize.c - XML Serialization Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * CRITICAL PRINCIPLES:
 * 1. Character-perfect output - must match input exactly
 * 2. No escaping in CDATA - CDATA content is literal
 * 3. Proper escaping elsewhere - text needs <>&"' escaped
 * 4. Document order - traverse children in correct order
 */

#include "serialize.h"
#include "../include/leptris.h"     /* LEPTRIS_API (visibility attribute) */
#include "../dom/node.h"            /* LeptrisNodeVTable + leptris_node_vtable_for */
#include "../leptris_internal.h"
#include "../common/entities.h"
#include "../common/string_view.h"
#include "../common/simd_text.h"
/* LeptrisSerializeOptions comes from leptris/types.h via leptris_internal.h's
 * pool.h include.  No local redefinition — it would conflict with the
 * public type. */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

/* Initial buffer capacity */
#define INITIAL_BUFFER_CAPACITY 1024

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

SerializeBuffer* buffer_create(int indent_spaces) {
    SerializeBuffer* buf = LEPTRIS_ALLOC(SerializeBuffer);
    if (!buf) return NULL;

    buf->capacity = INITIAL_BUFFER_CAPACITY;
    buf->data = LEPTRIS_ALLOC_N(char, buf->capacity);
    if (!buf->data) {
        LEPTRIS_FREE(buf);
        return NULL;
    }

    buf->size = 0;
    buf->data[0] = '\0';
    buf->indent = 0;
    buf->indent_spaces = indent_spaces;
    buf->alloc_failed = 0;

    return buf;
}

void buffer_ensure_capacity(SerializeBuffer* buf, size_t needed) {
    /* Invariant: buf->size <= buf->capacity.  Use subtraction so the
     * remaining-space check can never wrap. */
    if (needed <= buf->capacity - buf->size) return;

    /* Grow by doubling, overflow-safe.  If doubling would exceed SIZE_MAX,
     * clamp to SIZE_MAX — caller's append will fail to find space and the
     * alloc_failed flag will be set. */
    size_t new_cap = buf->capacity > 0 ? buf->capacity : INITIAL_BUFFER_CAPACITY;
    while (new_cap - buf->size < needed) {
        if (new_cap > (SIZE_MAX / 2)) {
            new_cap = SIZE_MAX;
            break;
        }
        new_cap *= 2;
    }

    /* If we still don't fit (only possible if new_cap == SIZE_MAX and the
     * request is genuinely too large), bail without touching buf->capacity. */
    if (new_cap - buf->size < needed) {
        buf->alloc_failed = 1;
        return;
    }

    char* new_data = LEPTRIS_REALLOC_N(buf->data, char, new_cap);
    if (!new_data) {
        /* Realloc failed: buf->data and buf->capacity are unchanged
         * (preserves the valid state).  Mark the failure so the caller
         * can detect it; subsequent appends will silently truncate. */
        buf->alloc_failed = 1;
        return;
    }

    buf->data = new_data;
    buf->capacity = new_cap;
}

void buffer_append(SerializeBuffer* buf, const char* str) {
    if (!str) return;

    size_t len = strlen(str);
    buffer_ensure_capacity(buf, len + 1);

    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
}

void buffer_append_len(SerializeBuffer* buf, const char* str, size_t len) {
    if (!str || len == 0) return;

    buffer_ensure_capacity(buf, len + 1);

    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
}

void buffer_append_char(SerializeBuffer* buf, char c) {
    buffer_ensure_capacity(buf, 2);

    buf->data[buf->size++] = c;
    buf->data[buf->size] = '\0';
}

void buffer_append_indent(SerializeBuffer* buf) {
    if (!buf || buf->indent_spaces <= 0) return;

    int spaces = buf->indent * buf->indent_spaces;
    buffer_ensure_capacity(buf, spaces + 1);

    for (int i = 0; i < spaces; i++) {
        buf->data[buf->size++] = ' ';
    }
    buf->data[buf->size] = '\0';
}

void buffer_append_newline(SerializeBuffer* buf) {
    if (!buf || buf->indent_spaces <= 0) return;
    buffer_append_char(buf, '\n');
}

char* buffer_to_string(SerializeBuffer* buf) {
    if (!buf) return NULL;

    /* Allocate exact size needed */
    char* result = LEPTRIS_ALLOC_N(char, buf->size + 1);
    if (result) {
        memcpy(result, buf->data, buf->size + 1);
    }

    return result;
}

void buffer_free(SerializeBuffer* buf) {
    if (buf) {
        if (buf->data) {
            LEPTRIS_FREE(buf->data);
        }
        LEPTRIS_FREE(buf);
    }
}

int buffer_has_error(SerializeBuffer* buf) {
    return buf ? buf->alloc_failed : 1;
}

/* ============================================================================
 * XML Entity Escaping
 * ============================================================================ */


/* Inline escaped-text emitter (TODO 194d): writes into a caller-
 * reserved worst-case buffer (6 bytes per input byte), preserving
 * the entity semantics of the previous per-run walkers — entity
 * references emit as-is (text mode), bare & escapes, quotes are
 * ordinary in text mode and escaped in attribute mode. */
/* Escape-class table (serialize endgame): the run scan was 3-4
 * compares per byte — the dominant serializer cost at 57% of the
 * text-heavy profile. One indexed load + one test per byte, the
 * same chartype-table shape as the parser (common/chartype.h) and
 * pugixml's g_escape_table. Bit 1 = must escape in text mode
 * (& < >), bit 2 = additional attr-mode chars (" '). */
static const unsigned char leptris_escape_table[256] = {
    ['&'] = 3, ['<'] = 3, ['>'] = 3,
    ['"'] = 2, ['\''] = 2,
};

/* Long-run threshold (round 20): three SIMD finds cost ~60ns of
 * dispatch; the per-byte table scan reaches that around 256 bytes
 * of run. Text-heavy documents (one multi-hundred-KB text node,
 * no specials) spent their entire serialize time in the per-byte
 * scan — this is the shape the matrix benchmark loses 1.27x on. */
#define ESCAPE_SIMD_RUN 256

static char* emit_escaped_inline(char* out, const char* content,
                                 size_t len, int attr_mode) {
    unsigned mode = attr_mode ? 3u : 1u;
    size_t i = 0;
    while (i < len) {
        size_t run = i;
        if (!attr_mode && len - i >= ESCAPE_SIMD_RUN) {
            /* Text mode over a long remainder: SIMD-find the first
             * of & < > (three passes beat one per-byte pass by an
             * order of magnitude at multi-KB lengths). */
            size_t rem = len - i;
            ptrdiff_t h1 = leptris_text_find(&content[i], rem, '&');
            ptrdiff_t h2 = leptris_text_find(&content[i], rem, '<');
            ptrdiff_t h3 = leptris_text_find(&content[i], rem, '>');
            ptrdiff_t best = -1;
            if (h1 >= 0) best = h1;
            if (h2 >= 0 && (best < 0 || h2 < best)) best = h2;
            if (h3 >= 0 && (best < 0 || h3 < best)) best = h3;
            if (best < 0) {
                memcpy(out, &content[i], rem);
                out += rem;
                break;
            }
            run = i + (size_t)best;
        } else {
            while (run < len &&
                   !(leptris_escape_table[(unsigned char)content[run]] & mode)) {
                run++;
            }
        }
        if (run > i) {
            memcpy(out, &content[i], run - i);
            out += run - i;
            i = run;
            if (i >= len) break;
        }

        if (content[i] == '&') {
            size_t j = i + 1;
            int found_semicolon = 0;
            while (j < len && j < i + 12) {
                if (content[j] == ';') { found_semicolon = 1; break; }
                if (!isalnum((unsigned char)content[j]) && content[j] != '#' && content[j] != '-') break;
                j++;
            }
            if (!attr_mode && found_semicolon && j > i + 1) {
                size_t n = j - i + 1;
                memcpy(out, &content[i], n);
                out += n;
                i = j + 1;
            } else {
                memcpy(out, "&amp;", 5);
                out += 5;
                i++;
            }
            continue;
        }
        if (content[i] == '<') { memcpy(out, "&lt;", 4); out += 4; }
        else if (content[i] == '>') { memcpy(out, "&gt;", 4); out += 4; }
        else if (attr_mode && content[i] == '"') { memcpy(out, "&quot;", 6); out += 6; }
        else { memcpy(out, "&apos;", 6); out += 6; }
        i++;
    }
    return out;
}

/* Escape special XML characters in text content.
 * Ordinary-character RUNS are bulk-appended (one capacity check +
 * one memcpy per run) instead of per-character appends — the
 * per-byte path was the serializer's dominant cost (TODO 194). */
/* ============================================================================
 * Node Serialization Functions
 * ============================================================================ */

void serialize_text_internal(LeptrisTextNode* text, SerializeBuffer* buf) {
    if (!text) return;

    /* Materialize + expand entities on first access. For borrowed
     * text nodes (the direct_parse fast path), this expands
     * predefined entities (&amp; → &) and, when a DTD is present
     * on the document, custom entities (&foo; → declared value).
     * Without this call the serializer reads raw borrowed bytes
     * and emits unexpanded entity references. */
    const char* content = leptris_text_get_content(text);
    if (!content) return;
    size_t content_len = text->content_len;

    /* Run-batched (TODO 194b): bulk-append ordinary bytes up to the
     * next special character; the entity lookahead only runs at '&'.
     * Quotes and apostrophes are ordinary in text content. */
    size_t i = 0;
    while (i < content_len) {
        size_t run = i;
        while (run < content_len) {
            char c = content[run];
            if (c == '&' || c == '<' || c == '>') break;
            run++;
        }
        if (run > i) {
            buffer_append_len(buf, &content[i], run - i);
            i = run;
            if (i >= content_len) break;
        }

        if (content[i] == '&') {
            /* Look ahead for ';' to detect entity reference */
            size_t j = i + 1;
            int found_semicolon = 0;
            while (j < text->content_len && j < i + 12) {
                if (content[j] == ';') {
                    found_semicolon = 1;
                    break;
                }
                if (!isalnum((unsigned char)content[j]) && content[j] != '#' && content[j] != '-') {
                    break;
                }
                j++;
            }
            if (found_semicolon && j > i + 1) {
                buffer_append_len(buf, &content[i], j - i + 1);
                i = j + 1;
            } else {
                buffer_append_len(buf, "&amp;", 5);
                i++;
            }
            continue;
        }

        if (content[i] == '<') {
            buffer_append_len(buf, "&lt;", 4);
        } else {
            buffer_append_len(buf, "&gt;", 4);
        }
        i++;
    }
}

void serialize_comment_internal(LeptrisCommentNode* comment, SerializeBuffer* buf) {
    if (!comment || !comment->content) return;

    buffer_append(buf, "<!--");
    buffer_append(buf, comment->content);
    buffer_append(buf, "-->");
}

void serialize_cdata_internal(LeptrisCDATANode* cdata, SerializeBuffer* buf) {
    if (!cdata || !cdata->content) return;

    buffer_append(buf, "<![CDATA[");
    /* XML 1.0 §2.7: "]]>" cannot appear inside a CDATA section.
     * Content containing the terminator is split across two CDATA
     * sections by rewriting it as "]]]]><![CDATA[>" — the same
     * technique libxml2 uses. Everything else in CDATA is literal. */
    const char* p = cdata->content;
    const char* run_start = p;
    while (*p) {
        if (p[0] == ']' && p[1] == ']' && p[2] == '>') {
            buffer_append_len(buf, run_start, (size_t)(p - run_start));
            buffer_append(buf, "]]]]><![CDATA[>");
            p += 3;
            run_start = p;
        } else {
            p++;
        }
    }
    buffer_append(buf, run_start);
    buffer_append(buf, "]]>");
}

void serialize_pi_internal(LeptrisPINode* pi, SerializeBuffer* buf) {
    if (!pi || !pi->target) return;

    buffer_append(buf, "<?");
    buffer_append(buf, pi->target);

    if (pi->data && pi->data[0] != '\0') {
        buffer_append_char(buf, ' ');
        buffer_append(buf, pi->data);
    }

    buffer_append(buf, "?>");
}

void serialize_doctype_internal(LeptrisDoctypeNode* doctype, SerializeBuffer* buf) {
    if (!doctype || !doctype->name) return;

    buffer_append(buf, "<!DOCTYPE ");
    buffer_append(buf, doctype->name);

    if (doctype->public_id) {
        buffer_append(buf, " PUBLIC \"");
        buffer_append(buf, doctype->public_id);
        buffer_append_char(buf, '"');

        if (doctype->system_id) {
            buffer_append(buf, " \"");
            buffer_append(buf, doctype->system_id);
            buffer_append_char(buf, '"');
        }
    } else if (doctype->system_id) {
        buffer_append(buf, " SYSTEM \"");
        buffer_append(buf, doctype->system_id);
        buffer_append_char(buf, '"');
    }

    /* Output internal subset if present */
    if (doctype->internal_subset) {
        buffer_append(buf, " [");
        buffer_append(buf, doctype->internal_subset);
        buffer_append_char(buf, ']');
    }

    buffer_append_char(buf, '>');
}

/* Recursive serializer (kept as the deep-tree fallback for the
 * iterative walker below). */
/* Emit a qualified element/attribute name: XML requires names in a
 * namespace to serialize with their prefix (prefix:name). libleptris
 * stores the prefix beside the local name, so every name-emission
 * site routes through this helper. */
static void append_qualified_name(SerializeBuffer* buf, const char* prefix,
                                  const char* name, size_t name_len) {
    if (prefix && prefix[0]) {
        buffer_append(buf, prefix);
        buffer_append_char(buf, ':');
    }
    buffer_append_len(buf, name, name_len);
}

static void serialize_element_recursive(LeptrisElement elem, SerializeBuffer* buf, int is_root) {
    if (!elem) return;

    /* Get element name - use cached string if available, otherwise use StringView directly */
    const char* elem_name;
    size_t elem_name_len;

    if (elem->name) {
        /* Use cached NULL-terminated string; name_len byte avoids
         * the per-element strlen (0xFF = long-name fallback). */
        elem_name = elem->name;
        elem_name_len = (elem->name_len != 0xFF)
            ? (size_t)elem->name_len : strlen(elem->name);
    } else {
        return;
    }

    const char* elem_prefix = leptris_element_get_prefix(elem);
    size_t elem_prefix_len =
        (elem_prefix && elem_prefix[0]) ? strlen(elem_prefix) + 1 : 0;

    /* Add indentation before opening tag (not for root element) */
    if (!is_root && buf->indent_spaces > 0) {
        buffer_append_indent(buf);
    }

    /* Opening tag — one reservation + inline emission (TODO 194c):
     * replaces separate '<' and name appends (two capacity checks
     * and two NUL stores per element). */
    buffer_ensure_capacity(buf, 1 + elem_prefix_len + elem_name_len + 1);
    char* ot = buf->data + buf->size;
    *ot++ = '<';
    if (elem_prefix_len) {
        memcpy(ot, elem_prefix, elem_prefix_len - 1);
        ot += elem_prefix_len - 1;
        *ot++ = ':';
    }
    memcpy(ot, elem_name, elem_name_len);
    ot += elem_name_len;
    buf->size = (size_t)(ot - buf->data);
    buf->data[buf->size] = '\0';

    /* Attributes - iterate through linked list */
    for (struct leptris_attribute* attr = leptris_element_get_first_attribute(elem); attr != NULL; attr = leptris_attr_next(attr)) {
        if (!attr) continue;

        /* Expand entity-containing values lazily before re-escaping
         * (single representation, round 4: the expanded copy REPLACES
         * the view so the raw '&' doesn't double-escape). */
        const char* val;
        if (attr_has_entities(attr)) {
            struct leptris_document* doc = leptris_element_get_document(elem);
            LeptrisMemoryPool* pool = doc ? doc->pool : NULL;
            char* resolved = pool
                ? leptris_decode_entities_view(&attr->value_view, pool)
                : NULL;
            if (resolved) {
                attr->value_view = leptris_sv_from_cstr(resolved);
                attr_set_entities(attr, 0);
            }
            val = attr_cvalue(attr);
        } else {
            val = attr_cvalue(attr);
        }

        /* One capacity reservation + raw emission per attribute
         * (TODO 194): worst-case escaping is 6 bytes per value byte,
         * so name + quotes + fully-escaped value fits one check.
         * Replaces five append calls (each with its own capacity
         * check and NUL store) per attribute. */
        const char* name_c = attr_cname(attr);
        size_t name_len = attr->name_view.length;
        const char* attr_prefix = attr_get_prefix(attr);
        size_t attr_prefix_len =
            (attr_prefix && attr_prefix[0]) ? strlen(attr_prefix) + 1 : 0;
        /* The view is authoritative: entity resolution REPLACES the
         * view (from_cstr), so length always matches the data. */
        size_t val_len = attr->value_view.length;
        size_t needed = 1 + attr_prefix_len + name_len + 2 + 6 * val_len + 2;
        buffer_ensure_capacity(buf, needed + 1);

        char* out = buf->data + buf->size;
        *out++ = ' ';
        if (attr_prefix_len) {
            memcpy(out, attr_prefix, attr_prefix_len - 1);
            out += attr_prefix_len - 1;
            *out++ = ':';
        }
        memcpy(out, name_c, name_len);
        out += name_len;
        *out++ = '=';
        *out++ = '"';
        for (size_t i = 0; i < val_len; i++) {
            switch (val[i]) {
                case '<':  memcpy(out, "&lt;", 4);   out += 4; break;
                case '>':  memcpy(out, "&gt;", 4);   out += 4; break;
                case '&':  memcpy(out, "&amp;", 5);  out += 5; break;
                case '"':  memcpy(out, "&quot;", 6); out += 6; break;
                case '\'': memcpy(out, "&apos;", 6); out += 6; break;
                default:   *out++ = val[i]; break;
            }
        }
        *out++ = '"';
        buf->size = (size_t)(out - buf->data);
        buf->data[buf->size] = '\0';
    }

    /* Namespaces - serialize as xmlns attributes. One reservation +
     * inline emission per namespace (TODO 194c): URIs rarely need
     * escaping, but the 6x worst-case bound is reserved anyway. */
    for (struct leptris_namespace* ns = leptris_elem_namespaces(elem); ns != NULL; ns = ns->next) {
        if (!ns) continue;

        const char* prefix = ns->prefix ? ns->prefix : "";
        size_t prefix_len = ns->prefix ? strlen(ns->prefix) : 0;
        const char* uri = ns->uri ? ns->uri : "";
        size_t uri_len = ns->uri ? strlen(ns->uri) : 0;
        buffer_ensure_capacity(buf, 7 + prefix_len + 2 + 6 * uri_len + 2);

        char* nw = buf->data + buf->size;
        *nw++ = ' ';
        memcpy(nw, "xmlns", 5);
        nw += 5;
        if (ns->prefix) {
            *nw++ = ':';
            memcpy(nw, prefix, prefix_len);
            nw += prefix_len;
        }
        *nw++ = '=';
        *nw++ = '"';
        for (size_t i = 0; i < uri_len; i++) {
            switch (uri[i]) {
                case '<':  memcpy(nw, "&lt;", 4);   nw += 4; break;
                case '>':  memcpy(nw, "&gt;", 4);   nw += 4; break;
                case '&':  memcpy(nw, "&amp;", 5);  nw += 5; break;
                case '"':  memcpy(nw, "&quot;", 6); nw += 6; break;
                case '\'': memcpy(nw, "&apos;", 6); nw += 6; break;
                default:   *nw++ = uri[i]; break;
            }
        }
        *nw++ = '"';
        buf->size = (size_t)(nw - buf->data);
        buf->data[buf->size] = '\0';
    }

    /* Check if element has children */
    if (leptris_node_first_child_internal((LeptrisNode*)elem)) {
        /* Check if this is a text-only element (single text child, no element children) */
        int is_text_only = 1;
        LeptrisNode* child = leptris_node_first_child_internal((LeptrisNode*)elem);

        /* Check if there's only one child and it's a text node */
        if (child && child->type == LEPTRIS_NODE_TYPE_TEXT) {
            /* Check if there are any siblings (more children) */
            if (leptris_node_get_next_sibling(child) != NULL) {
                is_text_only = 0;
            }
        } else {
            /* Not a single text node */
            is_text_only = 0;
        }

        if (is_text_only && buf->indent_spaces == 0) {
            /* Text-only element, compact mode: ONE reservation + inline
             * emission for the whole `<name>text</name>` (TODO 194d) —
             * was five buffer calls plus a node-dispatch hop. */
            LeptrisTextNode* tn = (LeptrisTextNode*)child;
            const char* tc;
            size_t tlen;
            if (tn->borrowed && tn->content_len > 0 &&
                !memchr(tn->content, '&', tn->content_len)) {
                tc = tn->content;
                tlen = tn->content_len;
            } else {
                tc = leptris_text_get_content(tn);
                tlen = tc ? tn->content_len : 0;
            }
            buffer_ensure_capacity(buf, 2 + elem_prefix_len + elem_name_len + 6 * tlen + 2 + 1);
            char* te = buf->data + buf->size;
            *te++ = '>';
            if (tc && tlen) {
                te = emit_escaped_inline(te, tc, tlen, 0);
            }
            *te++ = '<';
            *te++ = '/';
            if (elem_prefix_len) {
                memcpy(te, elem_prefix, elem_prefix_len - 1);
                te += elem_prefix_len - 1;
                *te++ = ':';
            }
            memcpy(te, elem_name, elem_name_len);
            te += elem_name_len;
            *te++ = '>';
            buf->size = (size_t)(te - buf->data);
            buf->data[buf->size] = '\0';
        } else if (is_text_only && buf->indent_spaces > 0) {
            /* Text-only element with indenting - serialize with newlines */
            /* Close opening tag */
            buffer_append_char(buf, '>');

            /* Serialize the single text child */
            serialize_node_internal(leptris_node_first_child_internal((LeptrisNode*)elem), buf);

            /* Closing tag */
            buffer_append(buf, "</");
            append_qualified_name(buf, elem_prefix, elem_name, elem_name_len);
            buffer_append_char(buf, '>');

            /* Add newline after closing tag when indenting */
            buffer_append_newline(buf);
        } else {
            /* Element has element children or multiple children - use pretty formatting */
            /* Close opening tag */
            buffer_append_char(buf, '>');

            /* Add newline after opening tag if indenting */
            if (buf->indent_spaces > 0) {
                buffer_append_newline(buf);
            }

            /* Increase indent level for children */
            buf->indent++;

            /* Serialize children */
            LeptrisNode* ser_child = leptris_node_first_child_internal((LeptrisNode*)elem);
            while (ser_child) {
                /* Pass is_root=0 for all children */
                if (ser_child->type == LEPTRIS_NODE_TYPE_ELEMENT) {
                    serialize_element_recursive((LeptrisElement)ser_child, buf, 0);
                } else {
                    serialize_node_internal(child, buf);
                }
                ser_child = leptris_node_get_next_sibling(ser_child);
            }

            /* Decrease indent level after children */
            buf->indent--;

            /* Add indentation before closing tag */
            if (buf->indent_spaces > 0) {
                buffer_append_indent(buf);
            }

            /* Closing tag */
            buffer_append(buf, "</");
            append_qualified_name(buf, elem_prefix, elem_name, elem_name_len);
            buffer_append_char(buf, '>');

            /* Add newline after closing tag if not root */
            if (!is_root && buf->indent_spaces > 0) {
                buffer_append_newline(buf);
            }
        }
    } else {
        /* Self-closing tag */
        buffer_append(buf, "/>");

        /* Add newline after self-closing tag if indenting and not root */
        if (!is_root && buf->indent_spaces > 0) {
            buffer_append_newline(buf);
        }
    }
}


/* ============================================================================
 * Iterative serializer (TODO 194e): one frame, explicit descent stack,
 * no per-child call — the recursive walker's call frame was ~15 ns per
 * element, the largest single item left in the text-heavy gap vs
 * pugixml. Element siblings stay in this frame; only DESCENT pushes.
 * Depth beyond the stack falls back to the recursive walker (which
 * also keeps unbounded-depth documents serializable).
 * ============================================================================
 */
#define SER_WALK_STACK_MAX 512

void serialize_element_internal(LeptrisElement root_elem, SerializeBuffer* buf, int is_root) {
    if (!root_elem || !root_elem->name) return;

    struct { LeptrisElement e; size_t nl; } st[SER_WALK_STACK_MAX];
    int sp = 0;

    LeptrisNode* cur = (LeptrisNode*)root_elem;
    int is_root_cur = is_root;

    for (;;) {
        if (cur->type != LEPTRIS_NODE_TYPE_ELEMENT) {
            serialize_node_internal(cur, buf);
            goto advance;
        }

        LeptrisElement e = (LeptrisElement)cur;
        const char* name = e->name;
        size_t nl = (e->name_len != 0xFF) ? (size_t)e->name_len
                                          : strlen(name);
        const char* epfx = leptris_element_get_prefix(e);
        size_t epl = (epfx && epfx[0]) ? strlen(epfx) + 1 : 0;

        /* --- total-fusion fast path (TODO 194f): a compact-mode leaf
         * element with no attributes and no namespaces emits its ENTIRE
         * `<name>text</name>` from one reservation — the dominant shape
         * in text-heavy documents paid two reservations + finalize. */
        if (leptris_element_get_first_attribute(e) == NULL &&
            leptris_elem_namespaces(e) == NULL) {
            LeptrisNode* fc0 = leptris_node_first_child_internal((LeptrisNode*)e);
            if (fc0 && fc0->type == LEPTRIS_NODE_TYPE_TEXT &&
                leptris_node_get_next_sibling(fc0) == NULL) {
                LeptrisTextNode* tn0 = (LeptrisTextNode*)fc0;
                /* View-direct (round 18): for entity-free borrowed
                 * text, read the view directly — get_content would
                 * materialize a pool copy (alloc + memcpy) just to
                 * NUL-terminate, which the escaper doesn't need
                 * (it's length-bounded). Entity-bearing text still
                 * materializes for correct expansion. */
                const char* tc0;
                size_t tl0;
                if (tn0->borrowed && tn0->content_len > 0 &&
                    !memchr(tn0->content, '&', tn0->content_len)) {
                    tc0 = tn0->content;
                    tl0 = tn0->content_len;
                } else {
                    tc0 = leptris_text_get_content(tn0);
                    tl0 = tc0 ? tn0->content_len : 0;
                }
                /* Pretty mode fuses newline + indent + open + text +
                 * close into the same single reservation — leaves
                 * otherwise pay ~8 capacity-checked appends each
                 * (the raw-only guard was why pretty trailed). */
                int pretty0 = buf->indent_spaces > 0;
                /* Lead = indent spaces ONLY: the parent's open tag
                 * (or the previous sibling's close) already emitted
                 * the newline that ended this line — a leading \n
                 * here inserts blank lines. Trail mirrors the close-
                 * tag site: newline after non-root closes. */
                int lead = (!is_root_cur && pretty0)
                    ? buf->indent * buf->indent_spaces : 0;
                int trail = (pretty0 && !is_root_cur) ? 1 : 0;
                buffer_ensure_capacity(
                    buf, (size_t)(lead + trail) + 2 * (epl + nl) + 6 * tl0 + 6);
                char* q = buf->data + buf->size;
                if (lead) {
                    memset(q, ' ', (size_t)lead);
                    q += lead;
                }
                *q++ = '<';
                if (epl) {
                    memcpy(q, epfx, epl - 1); q += epl - 1;
                    *q++ = ':';
                }
                memcpy(q, name, nl); q += nl;
                *q++ = '>';
                if (tc0 && tl0) q = emit_escaped_inline(q, tc0, tl0, 0);
                *q++ = '<'; *q++ = '/';
                if (epl) {
                    memcpy(q, epfx, epl - 1); q += epl - 1;
                    *q++ = ':';
                }
                memcpy(q, name, nl); q += nl;
                *q++ = '>';
                if (trail) *q++ = '\n';
                buf->size = (size_t)(q - buf->data);
                buf->data[buf->size] = '\0';
                goto advance;
            }
        }

        /* --- open tag (batched, as before) --- */
        if (!is_root_cur && buf->indent_spaces > 0) buffer_append_indent(buf);
        buffer_ensure_capacity(buf, 1 + epl + nl + 1);
        char* ot = buf->data + buf->size;
        *ot++ = '<';
        if (epl) {
            memcpy(ot, epfx, epl - 1);
            ot += epl - 1;
            *ot++ = ':';
        }
        memcpy(ot, name, nl);
        ot += nl;
        buf->size = (size_t)(ot - buf->data);
        buf->data[buf->size] = '\0';

        /* --- attributes (identical emission) --- */
        for (struct leptris_attribute* attr = leptris_element_get_first_attribute(e); attr; attr = leptris_attr_next(attr)) {
            const char* val;
            if (attr_has_entities(attr)) {
                struct leptris_document* d = leptris_element_get_document(e);
                LeptrisMemoryPool* pool = d ? d->pool : NULL;
                char* resolved = pool ? leptris_decode_entities_view(&attr->value_view, pool) : NULL;
                if (resolved) {
                    attr->value_view = leptris_sv_from_cstr(resolved);
                    attr_set_entities(attr, 0);
                }
                val = attr_cvalue(attr);
            } else {
                val = attr_cvalue(attr);
            }
            const char* name_c = attr_cname(attr);
            size_t anl = attr->name_view.length;
            const char* apfx = attr_get_prefix(attr);
            size_t apl = (apfx && apfx[0]) ? strlen(apfx) + 1 : 0;
            /* View length is authoritative (entity resolution
             * replaces the view) — same as the recursive path. */
            size_t vlen = attr->value_view.length;
            buffer_ensure_capacity(buf, 1 + apl + anl + 2 + 6 * vlen + 2 + 1);
            char* out = buf->data + buf->size;
            *out++ = ' ';
            if (apl) {
                memcpy(out, apfx, apl - 1);
                out += apl - 1;
                *out++ = ':';
            }
            memcpy(out, name_c, anl);
            out += anl;
            *out++ = '=';
            *out++ = '"';
            for (size_t i = 0; i < vlen; i++) {
                switch (val[i]) {
                    case '<':  memcpy(out, "&lt;", 4);   out += 4; break;
                    case '>':  memcpy(out, "&gt;", 4);   out += 4; break;
                    case '&':  memcpy(out, "&amp;", 5);  out += 5; break;
                    case '"':  memcpy(out, "&quot;", 6); out += 6; break;
                    case '\'': memcpy(out, "&apos;", 6); out += 6; break;
                    default:   *out++ = val[i]; break;
                }
            }
            *out++ = '"';
            buf->size = (size_t)(out - buf->data);
            buf->data[buf->size] = '\0';
        }

        /* --- namespaces (identical emission) --- */
        for (struct leptris_namespace* ns = leptris_elem_namespaces(e); ns; ns = ns->next) {
            const char* prefix = ns->prefix ? ns->prefix : "";
            size_t pnl = ns->prefix ? strlen(ns->prefix) : 0;
            const char* uri = ns->uri ? ns->uri : "";
            size_t ul = ns->uri ? strlen(ns->uri) : 0;
            buffer_ensure_capacity(buf, 7 + pnl + 2 + 6 * ul + 2);
            char* nw = buf->data + buf->size;
            *nw++ = ' ';
            memcpy(nw, "xmlns", 5);
            nw += 5;
            if (ns->prefix) { *nw++ = ':'; memcpy(nw, prefix, pnl); nw += pnl; }
            *nw++ = '=';
            *nw++ = '"';
            for (size_t i = 0; i < ul; i++) {
                switch (uri[i]) {
                    case '<':  memcpy(nw, "&lt;", 4);   nw += 4; break;
                    case '>':  memcpy(nw, "&gt;", 4);   nw += 4; break;
                    case '&':  memcpy(nw, "&amp;", 5);  nw += 5; break;
                    case '"':  memcpy(nw, "&quot;", 6); nw += 6; break;
                    case '\'': memcpy(nw, "&apos;", 6); nw += 6; break;
                    default:   *nw++ = uri[i]; break;
                }
            }
            *nw++ = '"';
            buf->size = (size_t)(nw - buf->data);
            buf->data[buf->size] = '\0';
        }

        /* --- children --- */
        LeptrisNode* fc = leptris_node_first_child_internal((LeptrisNode*)e);
        if (!fc) {
            buffer_append(buf, "/>");
            if (!is_root_cur && buf->indent_spaces > 0) buffer_append_newline(buf);
            goto advance;
        }
        if (fc->type == LEPTRIS_NODE_TYPE_TEXT &&
            leptris_node_get_next_sibling(fc) == NULL) {
            /* text-only element */
            if (buf->indent_spaces == 0) {
                LeptrisTextNode* tn = (LeptrisTextNode*)fc;
                const char* tc;
                size_t tlen;
                if (tn->borrowed && tn->content_len > 0 &&
                    !memchr(tn->content, '&', tn->content_len)) {
                    tc = tn->content;
                    tlen = tn->content_len;
                } else {
                    tc = leptris_text_get_content(tn);
                    tlen = tc ? tn->content_len : 0;
                }
                buffer_ensure_capacity(buf, 2 + epl + nl + 6 * tlen + 2 + 1);
                char* te = buf->data + buf->size;
                *te++ = '>';
                if (tc && tlen) te = emit_escaped_inline(te, tc, tlen, 0);
                *te++ = '<'; *te++ = '/';
                if (epl) {
                    memcpy(te, epfx, epl - 1); te += epl - 1;
                    *te++ = ':';
                }
                memcpy(te, name, nl); te += nl;
                *te++ = '>';
                buf->size = (size_t)(te - buf->data);
                buf->data[buf->size] = '\0';
            } else {
                buffer_append_char(buf, '>');
                serialize_node_internal(fc, buf);
                buffer_append(buf, "</");
                append_qualified_name(buf, epfx, name, nl);
                buffer_append_char(buf, '>');
                buffer_append_newline(buf);
            }
            goto advance;
        }

        /* complex children: descend (stack overflow guard -> recurse) */
        if (sp == SER_WALK_STACK_MAX) {
            buffer_append_char(buf, '>');
            if (buf->indent_spaces > 0) buffer_append_newline(buf);
            buf->indent++;
            LeptrisNode* c = fc;
            while (c) {
                if (c->type == LEPTRIS_NODE_TYPE_ELEMENT) {
                    serialize_element_recursive((LeptrisElement)c, buf, 0);
                } else {
                    serialize_node_internal(c, buf);
                }
                c = leptris_node_get_next_sibling(c);
            }
            buf->indent--;
            if (buf->indent_spaces > 0) buffer_append_indent(buf);
            buffer_append(buf, "</");
            append_qualified_name(buf, epfx, name, nl);
            buffer_append_char(buf, '>');
            if (!is_root_cur && buf->indent_spaces > 0) buffer_append_newline(buf);
            goto advance;
        }

        buffer_append_char(buf, '>');
        if (buf->indent_spaces > 0) buffer_append_newline(buf);
        st[sp].e = e;
        st[sp].nl = nl;
        sp++;
        buf->indent++;
        cur = fc;
        is_root_cur = 0;
        continue;

    advance:
        /* next sibling, else ascend closing frames */
        for (;;) {
            LeptrisNode* nsib = leptris_node_get_next_sibling(cur);
            if (nsib) {
                cur = nsib;
                break;
            }
            if (sp == 0) return;  /* root fully emitted */
            sp--;
            buf->indent--;
            LeptrisElement pe = st[sp].e;
            const char* pn = pe->name;
            size_t pnl2 = st[sp].nl;
            const char* pfx2 = leptris_element_get_prefix(pe);
            if (buf->indent_spaces > 0) buffer_append_indent(buf);
            buffer_append(buf, "</");
            append_qualified_name(buf, pfx2, pn, pnl2);
            buffer_append_char(buf, '>');
            if (buf->indent_spaces > 0 && !(sp == 0 && (LeptrisNode*)pe == (LeptrisNode*)root_elem && is_root)) {
                buffer_append_newline(buf);
            }
            if (sp == 0 && (LeptrisNode*)pe == (LeptrisNode*)root_elem) {
                /* root closed; but root may itself have siblings if the
                 * caller passed a non-root — handled: loop continues to
                 * root's next sibling check at top of advance loop */
            }
            cur = (LeptrisNode*)pe;
        }
    }
}

void serialize_node_internal(LeptrisNode* node, SerializeBuffer* buf) {
    if (!node) return;

    /* Dispatch via the per-type vtable registry — adding a new node
     * type is purely additive (register a vtable in node_vtable.c). */
    const LeptrisNodeVTable* vt = leptris_node_vtable_for(node->type);
    if (vt && vt->serialize) {
        vt->serialize(node, buf);
    }
    /* Unknown/unregistered type — silently skip (matches old default). */
}

/* ============================================================================
 * Public API
 * ============================================================================ */

char* leptris_serialize_node(LeptrisNode* node) {
    if (!node) return NULL;

    SerializeBuffer* buf = buffer_create(0);  /* Compact mode */
    if (!buf) return NULL;

    serialize_node_internal(node, buf);

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

char* leptris_serialize_element(LeptrisElement elem) {
    if (!elem) return NULL;

    SerializeBuffer* buf = buffer_create(0);  /* Compact mode */
    if (!buf) return NULL;

    serialize_element_internal(elem, buf, 1);  /* is_root=1 */

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

/* ============================================================================
 * Utility: Serialize with XML Declaration
 * ============================================================================ */

char* leptris_serialize_document_with_declaration(LeptrisElement root,
                                                   const char* encoding,
                                                   const char* version,
                                                   int standalone,
                                                   int has_bom,
                                                   LeptrisDoctypeNode* doctype) {
    if (!root) return NULL;

    SerializeBuffer* buf = buffer_create(0);  /* Compact mode by default */
    if (!buf) return NULL;

    /* Output UTF-8 BOM if present in original */
    if (has_bom) {
        buffer_append_char(buf, (char)(unsigned char)0xEF);
        buffer_append_char(buf, (char)(unsigned char)0xBB);
        buffer_append_char(buf, (char)(unsigned char)0xBF);
    }

    /* Add XML declaration only if version is provided */
    if (version) {
        buffer_append(buf, "<?xml version=\"");
        buffer_append(buf, version);
        buffer_append_char(buf, '"');

        if (encoding) {
            buffer_append(buf, " encoding=\"");
            buffer_append(buf, encoding);
            buffer_append_char(buf, '"');
        }

        if (standalone >= 0) {
            buffer_append(buf, " standalone=\"");
            buffer_append(buf, standalone ? "yes" : "no");
            buffer_append_char(buf, '"');
        }

        buffer_append(buf, "?>");
    }

    /* Output DOCTYPE if present */
    if (doctype) {
        if (version) {
            buffer_append_char(buf, '\n');
        }
        serialize_doctype_internal(doctype, buf);
    }

    /* Serialize root element */
    if (version || doctype) {
        buffer_append_char(buf, '\n');
    }
    serialize_element_internal(root, buf, 1);  /* is_root=1 */

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}
/* ============================================================================
 * Document Serialization (matches leptris.h public API)
 * ============================================================================ */

/* Forward declarations for document structure access */
struct leptris_document;

/* Serialize document with options */
LEPTRIS_API char* leptris_document_serialize(struct leptris_document* doc,
                                 LeptrisSerializeOptions* options) {
    if (!doc) return NULL;

    /* FlatDoc serialize fast path removed — direct_parse builds the
     * LeptrisElement tree eagerly. Serialization always walks the DOM. */

    /* Get root element from new_dom_root field */
    /* TODO 139 Phase D: trigger lazy promote if the doc was produced
     * by the flat-parse fast path. */
    leptris_document_ensure_promoted(doc);
    LeptrisElement root = (LeptrisElement)doc->new_dom_root;
    if (!root) return NULL;

    /* Use default options if NULL */
    int xml_declaration = 0;
    int indent_spaces = 0;
    const char* encoding = NULL;

    if (options) {
        xml_declaration = options->xml_declaration;
        indent_spaces = options->indent;
        encoding = options->encoding;
    }

    /* Create buffer with indent support */
    SerializeBuffer* buf = buffer_create(indent_spaces);
    if (!buf) return NULL;

    /* Output UTF-8 BOM if present in original */
    if (doc->has_bom) {
        buffer_append_char(buf, (char)(unsigned char)0xEF);
        buffer_append_char(buf, (char)(unsigned char)0xBB);
        buffer_append_char(buf, (char)(unsigned char)0xBF);
    }

    /* Add XML declaration if requested or if doc had one */
    const char* xml_version = doc->xml_version;
    int standalone = doc->standalone;

    /* If xml_declaration is explicitly requested but doc has no version,
     * fall back to XML 1.0 with no standalone attribute. */
    if (xml_declaration && !xml_version) {
        xml_version = "1.0";
        standalone = -1;
    }

    if ((xml_declaration || (doc->had_declaration && xml_version)) && xml_version) {
        buffer_append(buf, "<?xml version=\"");
        buffer_append(buf, xml_version);
        buffer_append_char(buf, '"');

        const char* enc = encoding ? encoding : doc->encoding;
        if (enc) {
            buffer_append(buf, " encoding=\"");
            buffer_append(buf, enc);
            buffer_append_char(buf, '"');
        }

        if (standalone >= 0) {
            buffer_append(buf, " standalone=\"");
            buffer_append(buf, standalone ? "yes" : "no");
            buffer_append_char(buf, '"');
        }

        buffer_append(buf, "?>");

        /* Add newline after declaration if indenting */
        if (indent_spaces > 0) {
            buffer_append_newline(buf);
        }
    }

    /* Output DOCTYPE if present */
    if (doc->doctype) {
        serialize_doctype_internal((LeptrisDoctypeNode*)doc->doctype, buf);
        if (indent_spaces > 0) {
            buffer_append_newline(buf);
        }
    }

    /* Output document-level processing instructions.  These are PIs
     * that appeared before or after the root element in the original
     * document (e.g. <?xml-stylesheet?>).  Order is preserved by the
     * parser appending to a linked list. */
    for (struct leptris_processing_instruction* pi = doc->pis;
         pi;
         pi = pi->next) {
        buffer_append(buf, "<?");
        if (pi->target) buffer_append(buf, pi->target);
        if (pi->data && pi->data[0]) {
            buffer_append_char(buf, ' ');
            buffer_append(buf, pi->data);
        }
        buffer_append(buf, "?>");
        if (indent_spaces > 0) {
            buffer_append_newline(buf);
        }
    }

    /* Serialize root element */
    serialize_element_internal(root, buf, 1);  /* is_root=1 */

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

/* ============================================================================
 * Element Serialization (matches leptris.h public API)
 * ============================================================================ */

/* Serialize element subtree to XML string */
LEPTRIS_API char* leptris_element_serialize(LeptrisElement elem,
                                 LeptrisSerializeOptions* options) {
    if (!elem) return NULL;

    int indent_spaces = 0;
    if (options) {
        indent_spaces = options->indent;
    }

    SerializeBuffer* buf = buffer_create(indent_spaces);
    if (!buf) return NULL;

    serialize_element_internal(elem, buf, 1);  /* is_root=1 */

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

/* Save document to file */
LEPTRIS_API int leptris_document_save_file(struct leptris_document* doc,
                               const char* filepath,
                               LeptrisSerializeOptions* options) {
    if (!doc) return -4;  /* LEPTRIS_ERROR_NULL_ARG */
    if (!filepath) return -4;  /* LEPTRIS_ERROR_NULL_ARG */

    /* Serialize document to XML string */
    char* xml = leptris_document_serialize(doc, options);
    if (!xml) return -1;  /* LEPTRIS_ERROR_MEMORY */

    /* Open file for writing */
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        LEPTRIS_FREE(xml);
        return -7;  /* LEPTRIS_ERROR_IO */
    }

    /* Write XML content to file */
    size_t len = strlen(xml);
    size_t written = fwrite(xml, 1, len, file);
    fclose(file);

    /* Free the XML string */
    LEPTRIS_FREE(xml);

    /* Check if all bytes were written */
    if (written != len) {
        return -7;  /* LEPTRIS_ERROR_IO */
    }

    return 0;  /* LEPTRIS_OK */
}
