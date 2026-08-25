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
    /* Control characters escape as numeric references: attributes
     * escape \t \n \r (attribute-value normalization would eat
     * them); text escapes \r (newline translation). libxslt-
     * verified behavior. Rare: pre-scan and take the plain path
     * when absent so the SIMD hot loop is untouched. */
    int has_ctrl = 0;
    for (size_t k = 0; k < len; k++) {
        char c = content[k];
        if (c == '\r' || (attr_mode && (c == '\t' || c == '\n'))) {
            has_ctrl = 1;
            break;
        }
    }
    size_t i = 0;
    if (has_ctrl) {
        while (i < len) {
            char c = content[i];
            if (c == '\r' || (attr_mode && (c == '\t' || c == '\n'))) {
                out += sprintf(out, "&#%d;", (int)(unsigned char)c);
            } else if (c == '&') {
                memcpy(out, "&amp;", 5); out += 5;
            } else if (c == '<') {
                memcpy(out, "&lt;", 4); out += 4;
            } else if (c == '>') {
                memcpy(out, "&gt;", 4); out += 4;
            } else if (attr_mode && c == '"') {
                memcpy(out, "&quot;", 6); out += 6;
            } else if (attr_mode && c == '\'') {
                memcpy(out, "&apos;", 6); out += 6;
            } else {
                *out++ = c;
            }
            i++;
        }
        return out;
    }
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

/* §16.1 cdata-section-elements: does this element's QName match? */
static int is_cdata_element(SerializeBuffer* buf, LeptrisElement e) {
    if (!buf || !buf->cdata_count || !e) return 0;
    const char* local = leptris_element_get_name(e);
    if (!local) return 0;
    const char* prefix = leptris_element_get_prefix(e);
    int has_pfx = prefix && *prefix;
    char qn[256];
    if (has_pfx)
        snprintf(qn, sizeof(qn), "%s:%s", prefix, local);
    else
        snprintf(qn, sizeof(qn), "%s", local);
    for (size_t i = 0; i < buf->cdata_count; i++) {
        if (!buf->cdata_names[i]) continue;
        if (strcmp(buf->cdata_names[i], qn) == 0) return 1;
        /* An unprefixed entry also matches a no-namespace element. */
        if (!strchr(buf->cdata_names[i], ':') && !has_pfx &&
            strcmp(buf->cdata_names[i], local) == 0) return 1;
    }
    return 0;
}

/* Emit the CDATA BODY, splitting "]]>" across section closes. The
 * caller owns the surrounding <![CDATA[ / ]]> brackets (run merging). */
static void emit_cdata_body(SerializeBuffer* buf, const char* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        const char* close = NULL;
        for (size_t j = i; j + 2 < len; j++) {
            if (s[j] == ']' && s[j+1] == ']' && s[j+2] == '>') {
                close = s + j;
                break;
            }
        }
        size_t chunk = close ? (size_t)(close - (s + i)) + 2 : len - i;
        buffer_append_len(buf, s + i, chunk);
        if (close) buffer_append(buf, "]]><![CDATA[");
        i += chunk + (close ? 1 : 0);
    }
}

/* Standalone single-node CDATA emission (fallback sites). */
static void emit_cdata(SerializeBuffer* buf, const char* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        const char* close = NULL;
        for (size_t j = i; j + 2 < len; j++) {
            if (s[j] == ']' && s[j+1] == ']' && s[j+2] == '>') {
                close = s + j;
                break;
            }
        }
        size_t chunk = close ? (size_t)(close - (s + i)) + 2 : len - i;
        buffer_append(buf, "<![CDATA[");
        buffer_append_len(buf, s + i, chunk);
        buffer_append(buf, "]]>");
        i += chunk + (close ? 1 : 0);
    }
}

/* libxslt rule: whitespace-only text is never wrapped in CDATA. */
static int text_is_ws_only(const char* s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return 0;
    }
    return 1;
}

/* Convert one text node's content per §16.1: CDATA unless the text
 * is whitespace-only (libxslt keeps ws plain). */
static void emit_cdata_checked(SerializeBuffer* buf, const char* s) {
    if (!s) return;
    size_t l = strlen(s);
    if (text_is_ws_only(s, l)) {
        buffer_append_len(buf, s, l);
        return;
    }
    emit_cdata(buf, s, l);
}


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

    /* disable-output-escaping (XSLT §16.4) / method=html script and
     * style content: emit the string-value verbatim — no entity
     * escaping, characters pass through as-is. */
    if (text->base.raw) {
        buffer_append_len(buf, content, content_len);
        return;
    }

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
        /* Issue #542: qualified name_view + separate prefix emission
         * would double the prefix — strip it from the name copy. */
        if (attr_prefix_len && name_len > attr_prefix_len &&
            memcmp(name_c, attr_prefix, attr_prefix_len - 1) == 0 &&
            name_c[attr_prefix_len - 1] == ':') {
            name_c += attr_prefix_len;
            name_len -= attr_prefix_len;
        }
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
                /* Attribute values escape control chars as numeric
                 * references (attribute-value normalization eats
                 * literal \t/\n/\r) — libxslt-verified. */
                case '\t': out += sprintf(out, "&#9;");  break;
                case '\n': out += sprintf(out, "&#10;"); break;
                case '\r': out += sprintf(out, "&#13;"); break;
                default:   *out++ = val[i]; break;
            }
        }
        *out++ = '"';
        buf->size = (size_t)(out - buf->data);
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

        if (is_text_only && is_cdata_element(buf, elem)) {
            LeptrisTextNode* tn = (LeptrisTextNode*)child;
            const char* tc = leptris_text_get_content(tn);
            buffer_append_char(buf, '>');
            if (tc) emit_cdata_checked(buf, tc);
            buffer_append(buf, "</");
            append_qualified_name(buf, elem_prefix, elem_name, elem_name_len);
            buffer_append_char(buf, '>');
        } else if (is_text_only && buf->indent_spaces == 0) {
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
                } else if (ser_child->type == LEPTRIS_NODE_TYPE_TEXT &&
                           !((LeptrisTextNode*)ser_child)->base.raw &&
                           is_cdata_element(buf, elem)) {
                    const char* tc =
                        leptris_text_get_content((LeptrisTextNode*)ser_child);
                    if (tc)
                        emit_cdata_checked(buf, tc);
                } else {
                    serialize_node_internal(ser_child, buf);
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

/* Issue #534: an element whose children include TEXT (non-whitespace)
 * or CDATA is MIXED CONTENT — indenting inside it would change the
 * text on round-trip (inserted whitespace becomes new text nodes on
 * reparse). libxml2 keeps such elements on one line; so do we.
 * Whitespace-only text nodes are formatting artifacts and do NOT
 * count (pretty documents keep indenting). */
static int ser_children_have_text(LeptrisNode* fc) {
    for (LeptrisNode* c = fc; c; c = leptris_node_get_next_sibling(c)) {
        if (c->type == LEPTRIS_NODE_TYPE_CDATA) return 1;
        if (c->type == LEPTRIS_NODE_TYPE_TEXT) {
            LeptrisTextNode* tn = (LeptrisTextNode*)c;
            const char* tc;
            size_t tl;
            if (tn->borrowed && tn->content_len > 0) {
                tc = tn->content;
                tl = tn->content_len;
            } else {
                tc = leptris_text_get_content(tn);
                tl = tc ? tn->content_len : 0;
            }
            for (size_t i = 0; i < tl; i++) {
                if (tc[i] != ' ' && tc[i] != '\t' &&
                    tc[i] != '\n' && tc[i] != '\r') {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Issue #546: rootless documents may still carry document-level
 * processing instructions. Emit the declaration (if the original had
 * one) and the PIs instead of returning an empty string. */
static char* serialize_rootless_pis(struct leptris_document* doc,
                                    const LeptrisSerializeOptions* options) {
    if (!doc || (!doc->pis && !doc->top_comments)) return NULL;
    SerializeBuffer* pibuf = buffer_create(0);
    if (!pibuf) return NULL;
    int xml_declaration = options ? options->xml_declaration : 1;
    if (xml_declaration && doc->had_declaration) {
        const char* enc = options && options->encoding
                           ? options->encoding
                           : (doc->encoding ? doc->encoding : "UTF-8");
        const char* ver = doc->xml_version ? doc->xml_version : "1.0";
        buffer_append(pibuf, "<?xml version=\"");
        buffer_append(pibuf, ver);
        buffer_append(pibuf, "\"");
        if (enc) {
            buffer_append(pibuf, " encoding=\"");
            buffer_append(pibuf, enc);
            buffer_append(pibuf, "\"");
        }
        if (doc->standalone >= 0) {
            buffer_append(pibuf, " standalone=\"");
            buffer_append(pibuf, doc->standalone ? "yes" : "no");
            buffer_append(pibuf, "\"");
        }
        buffer_append(pibuf, "?>");
    }
    for (struct leptris_processing_instruction* pi = doc->pis;
         pi; pi = pi->next) {
        buffer_append(pibuf, "<?");
        if (pi->target) buffer_append(pibuf, pi->target);
        if (pi->data && pi->data[0]) {
            buffer_append_char(pibuf, ' ');
            buffer_append(pibuf, pi->data);
        }
        buffer_append(pibuf, "?>");
    }
    /* Top-level comments (outside root) — the doc->pis twin. */
    for (struct leptris_top_comment* tc = doc->top_comments;
         tc; tc = tc->next) {
        buffer_append(pibuf, "<!--");
        if (tc->content) buffer_append(pibuf, tc->content);
        buffer_append(pibuf, "-->");
    }
    char* out = buffer_to_string(pibuf);
    buffer_free(pibuf);
    return out;
}

void serialize_element_internal(LeptrisElement root_elem, SerializeBuffer* buf, int is_root) {
    if (!root_elem || !root_elem->name) return;

    struct { LeptrisElement e; size_t nl; int mixed; int cd_open; } st[SER_WALK_STACK_MAX];
    int sp = 0;

    LeptrisNode* cur = (LeptrisNode*)root_elem;
    int is_root_cur = is_root;

    for (;;) {
        if (cur->type != LEPTRIS_NODE_TYPE_ELEMENT) {
            /* #534 companion: in pretty mode the formatter owns the
             * whitespace between elements of a NON-mixed parent —
             * the source's ws-only text nodes are dropped and
             * replaced by the canonical newline+indent the open and
             * close paths already emit. (Before, both were emitted,
             * doubling the blank lines on every round-trip.) Text
             * inside a mixed parent is verbatim content: never
             * dropped. */
            if (buf->indent_spaces > 0 &&
                cur->type == LEPTRIS_NODE_TYPE_TEXT &&
                !((sp > 0) && st[sp - 1].mixed)) {
                LeptrisTextNode* wsn = (LeptrisTextNode*)cur;
                const char* wsc;
                size_t wsl;
                if (wsn->borrowed && wsn->content_len > 0) {
                    wsc = wsn->content;
                    wsl = wsn->content_len;
                } else {
                    wsc = leptris_text_get_content(wsn);
                    wsl = wsc ? wsn->content_len : 0;
                }
                int ws_only = 1;
                for (size_t i = 0; i < wsl; i++) {
                    if (wsc[i] != ' ' && wsc[i] != '\t' &&
                        wsc[i] != '\n' && wsc[i] != '\r') {
                        ws_only = 0;
                        break;
                    }
                }
                if (ws_only) goto advance;   /* skip, formatter owns it */
            }
            /* §16.1 cdata-section-elements: text children of a
             * listed element join ONE CDATA run per consecutive
             * text span (libxslt merges the section). */
            int cdata_parent = sp > 0 && st[sp - 1].e &&
                               is_cdata_element(buf, st[sp - 1].e);
            if ((cur->type == LEPTRIS_NODE_TYPE_TEXT &&
                 !((LeptrisTextNode*)cur)->base.raw) ||
                (cur->type == LEPTRIS_NODE_TYPE_CDATA)) {
                if (cdata_parent) {
                    const char* tc =
                        (cur->type == LEPTRIS_NODE_TYPE_CDATA)
                            ? leptris_cdata_get_content(
                                  (LeptrisCDATANode*)cur)
                            : leptris_text_get_content(
                                  (LeptrisTextNode*)cur);
                    if (!st[sp - 1].cd_open) {
                        buffer_append(buf, "<![CDATA[");
                        st[sp - 1].cd_open = 1;
                    }
                    if (tc) emit_cdata_body(buf, tc, strlen(tc));
                    goto advance;
                }
                if (sp > 0 && st[sp - 1].cd_open) {
                    buffer_append(buf, "]]>");
                    st[sp - 1].cd_open = 0;
                }
                serialize_node_internal(cur, buf);
                goto advance;
            }
            if (sp > 0 && st[sp - 1].cd_open) {
                buffer_append(buf, "]]>");
                st[sp - 1].cd_open = 0;
            }
            serialize_node_internal(cur, buf);
            goto advance;
        }

        LeptrisElement e = (LeptrisElement)cur;
        /* #534: children of a mixed-content parent emit inline. */
        int parent_mixed = (sp > 0) && st[sp - 1].mixed;
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
                /* §16.1 cdata-section-elements wins over the fused
                 * escaped emission. */
                if (buf->cdata_count && !((LeptrisTextNode*)fc0)->base.raw &&
                    is_cdata_element(buf, e)) {
                    const char* tcd =
                        leptris_text_get_content((LeptrisTextNode*)fc0);
                    buffer_append_char(buf, '<');
                    append_qualified_name(
                        buf, leptris_element_get_prefix(e), name, nl);
                    buffer_append_char(buf, '>');
                    if (tcd)
                        emit_cdata_checked(buf, tcd);
                    buffer_append(buf, "</");
                    append_qualified_name(
                        buf, leptris_element_get_prefix(e),
                        name, nl);
                    buffer_append_char(buf, '>');
                    goto advance;
                }
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
                int pretty0 = buf->indent_spaces > 0 && !parent_mixed;
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
        if (!is_root_cur && buf->indent_spaces > 0 && !parent_mixed)
            buffer_append_indent(buf);
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

        /* --- namespaces (identical emission) --- */
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
            /* Issue #542: name_view carries the QUALIFIED name —
             * when the prefix is emitted separately, skip its bytes
             * so the prefix is not doubled (ns:ns:attr). */
            if (apl && anl > apl &&
                memcmp(name_c, apfx, apl - 1) == 0 && name_c[apl - 1] == ':') {
                name_c += apl;
                anl -= apl;
            }
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

        /* --- children --- */
        LeptrisNode* fc = leptris_node_first_child_internal((LeptrisNode*)e);
        if (!fc) {
            buffer_append(buf, "/>");
            if (!is_root_cur && buf->indent_spaces > 0 && !parent_mixed)
                buffer_append_newline(buf);
            goto advance;
        }
        if (fc->type == LEPTRIS_NODE_TYPE_TEXT &&
            leptris_node_get_next_sibling(fc) == NULL) {
            /* text-only element */
            if (is_cdata_element(buf, e) &&
                !((LeptrisTextNode*)fc)->base.raw) {
                buffer_append_char(buf, '>');
                const char* tcd = leptris_text_get_content(
                    (LeptrisTextNode*)fc);
                if (tcd)
                    emit_cdata_checked(buf, tcd);
                buffer_append(buf, "</");
                append_qualified_name(buf, epfx, name, nl);
                buffer_append_char(buf, '>');
                goto advance;
            }
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
                if (is_cdata_element(buf, e) &&
                    (fc->type == LEPTRIS_NODE_TYPE_TEXT ||
                     fc->type == LEPTRIS_NODE_TYPE_CDATA) &&
                    !((LeptrisTextNode*)fc)->base.raw) {
                    const char* tc2 =
                        leptris_text_get_content((LeptrisTextNode*)fc);
                    if (tc2)
                        emit_cdata_checked(buf, tc2);
                } else {
                    serialize_node_internal(fc, buf);
                }
                buffer_append(buf, "</");
                append_qualified_name(buf, epfx, name, nl);
                buffer_append_char(buf, '>');
                if (!parent_mixed) buffer_append_newline(buf);
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
                } else if (c->type == LEPTRIS_NODE_TYPE_TEXT &&
                           !((LeptrisTextNode*)c)->base.raw &&
                           is_cdata_element(buf, e)) {
                    const char* tc2 =
                        leptris_text_get_content((LeptrisTextNode*)c);
                    if (tc2)
                        emit_cdata_checked(buf, tc2);
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

        /* An element child interrupts any CDATA run of this parent. */
        if (sp > 0 && st[sp - 1].cd_open) {
            buffer_append(buf, "]]>");
            st[sp - 1].cd_open = 0;
        }
        buffer_append_char(buf, '>');
        int mixed = 0;
        if (buf->indent_spaces > 0) mixed = ser_children_have_text(fc);
        if (buf->indent_spaces > 0 && !mixed) buffer_append_newline(buf);
        st[sp].e = e;
        st[sp].nl = nl;
        st[sp].mixed = mixed;
        st[sp].cd_open = 0;
        sp++;
        buf->indent++;
        cur = fc;
        is_root_cur = 0;
        continue;

    advance:
        /* The walk's root completed (any completion path — close-tag,
         * leaf fast path, text-only, self-closing). Emit exactly the
         * caller's subtree: do NOT continue to the root's siblings
         * (issue #523 — the leak made element serialization both
         * wrong and O(document)). */
        if (sp == 0 && (LeptrisNode*)cur == (LeptrisNode*)root_elem) return;

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
            if (st[sp].cd_open) {   /* close this element's CDATA run */
                buffer_append(buf, "]]>");
                st[sp].cd_open = 0;
            }
            const char* pn = pe->name;
            size_t pnl2 = st[sp].nl;
            const char* pfx2 = leptris_element_get_prefix(pe);
            /* #534: no indent inside a mixed-content element. */
            if (buf->indent_spaces > 0 && !st[sp].mixed)
                buffer_append_indent(buf);
            buffer_append(buf, "</");
            append_qualified_name(buf, pfx2, pn, pnl2);
            buffer_append_char(buf, '>');
            if (buf->indent_spaces > 0 && !(sp == 0 && (LeptrisNode*)pe == (LeptrisNode*)root_elem && is_root)) {
                buffer_append_newline(buf);
            }
            if (sp == 0 && (LeptrisNode*)pe == (LeptrisNode*)root_elem) {
                /* The walk's root frame closed. STOP: an element
                 * serialize must emit exactly the caller's subtree —
                 * continuing to the root's siblings leaked every
                 * following element into the output (issue #523:
                 * both the garbage results and the O(document)
                 * per-call cost). */
                return;
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
                                 const LeptrisSerializeOptions* options) {
    if (!doc) return NULL;

    /* FlatDoc serialize fast path removed — direct_parse builds the
     * LeptrisElement tree eagerly. Serialization always walks the DOM. */

    /* Get root element from new_dom_root field */
    /* TODO 139 Phase D: trigger lazy promote if the doc was produced
     * by the flat-parse fast path. */
    leptris_document_ensure_promoted(doc);
    LeptrisElement root = (LeptrisElement)doc->new_dom_root;
    if (!root) {
        char* rootless = serialize_rootless_pis(doc, options);
        if (rootless) return rootless;
        return NULL;
    }

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
    if (options) {
        buf->cdata_names = options->cdata_elements;
        buf->cdata_count = options->cdata_element_count;
    }

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

        /* TODO.bindings/06: with iconv (default build) the body was
         * transcoded to UTF-8 at parse time — the declaration says
         * so, always. Without iconv, bytes pass through unchanged:
         * echoing the original encoding stays truthful. Either way
         * the declaration never lies, and serialize(serialize(x))
         * is byte-stable. */
        const char* enc = encoding ? encoding : doc->encoding;
        if (enc) {
            buffer_append(buf, " encoding=\"");
#ifdef LEPTRIS_HAS_ICONV
            if (enc[0] == 'U' || enc[0] == 'u') {
                buffer_append(buf, enc);
            } else {
                buffer_append(buf, "UTF-8");
            }
#else
            buffer_append(buf, enc);
#endif
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

    /* Top-level comments (outside root) — emitted before the root,
     * after doc-level PIs. Each chain preserves its own parse order;
     * cross-chain interleaving is not tracked (same limitation as
     * the PI chain). */
    for (struct leptris_top_comment* tc = doc->top_comments;
         tc;
         tc = tc->next) {
        buffer_append(buf, "<!--");
        if (tc->content) buffer_append(buf, tc->content);
        buffer_append(buf, "-->");
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
                                 const LeptrisSerializeOptions* options) {
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

/* Issue #541: mem-cache the serialized string so the size-query
 * + into-call pattern reuses a single serialization. */
static int ser_cache_valid(const struct leptris_document* doc,
                           const LeptrisSerializeOptions* options) {
    if (!doc->ser_cache) return 0;
    if (doc->ser_version != doc->ser_cache_version) return 0;
    int text = options ? !options->xml_declaration : 1;
    int omit = options ? !options->xml_declaration : 0;
    int indent = options ? !!options->indent : 0;
    return text == doc->ser_cache_text && omit == doc->ser_cache_omit &&
           indent == doc->ser_cache_indent;
}

LEPTRIS_API size_t leptris_document_serialize_into(LeptrisDocument doc,
                                                   char* buf,
                                                   size_t capacity,
                                                   size_t* out_len,
                                                   const LeptrisSerializeOptions* options) {
    if (!doc) return 0;
    if (ser_cache_valid(doc, options)) {
        size_t need = doc->ser_cache_len + 1;
        if (buf && capacity >= need) {
            memcpy(buf, doc->ser_cache, need);
            if (out_len) *out_len = doc->ser_cache_len;
        }
        return need;
    }
    char* tmp = leptris_document_serialize(doc, options);
    if (!tmp) return 0;
    size_t need = strlen(tmp) + 1;
    if (buf && capacity >= need) {
        memcpy(buf, tmp, need);
        if (out_len) *out_len = need - 1;
    }
    /* Cache for the no-options (defaults) path; option permutations
     * stay one-shot to keep the cache key simple. */
    if (!options) {
        free(doc->ser_cache);
        doc->ser_cache = tmp;                 /* ownership transferred */
        doc->ser_cache_len = need - 1;
        doc->ser_cache_version = doc->ser_version;
        doc->ser_cache_text = 0;
        doc->ser_cache_omit = 0;
        doc->ser_cache_indent = 0;
    } else {
        leptris_free_string(tmp);
    }
    return need;
}

LEPTRIS_API size_t leptris_element_serialize_into(LeptrisElement elem,
                                                  char* buf,
                                                  size_t capacity,
                                                  size_t* out_len,
                                                  const LeptrisSerializeOptions* options) {
    char* tmp = leptris_element_serialize(elem, options);
    if (!tmp) return 0;
    size_t need = strlen(tmp) + 1;
    if (buf && capacity >= need) {
        memcpy(buf, tmp, need);
        if (out_len) *out_len = need - 1;
    }
    leptris_free_string(tmp);
    return need;
}

/* Save document to file */
LEPTRIS_API int leptris_document_save_file(struct leptris_document* doc,
                               const char* filepath,
                               const LeptrisSerializeOptions* options) {
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
