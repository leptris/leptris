/* flat/flat_serialize.c — FlatDoc-direct XML serialization (TODO 145 Phase 2).
 *
 * Walks the FlatDoc node array directly to produce an XML string,
 * bypassing the compact-pointer promote pass. The output matches
 * what taurus_document_serialize would produce for the same input.
 */
#include "flat_serialize.h"
#include "flat_doc.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Growable output buffer. */
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} FlatOutBuf;

static int buf_reserve(FlatOutBuf* b, size_t extra) {
    if (b->size + extra + 1 <= b->capacity) return 0;
    size_t new_cap = b->capacity ? b->capacity * 2 : 256;
    while (new_cap < b->size + extra + 1) new_cap *= 2;
    char* p = (char*)realloc(b->data, new_cap);
    if (!p) return -1;
    b->data = p;
    b->capacity = new_cap;
    return 0;
}

static int buf_append(FlatOutBuf* b, const char* s, size_t n) {
    if (buf_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->size, s, n);
    b->size += n;
    b->data[b->size] = '\0';
    return 0;
}

static int buf_append_char(FlatOutBuf* b, char c) {
    return buf_append(b, &c, 1);
}

static int buf_append_escaped_attr(FlatOutBuf* b, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"') {
            if (buf_append(b, "&quot;", 6) != 0) return -1;
        } else if (c == '<') {
            if (buf_append(b, "&lt;", 4) != 0) return -1;
        } else if (c == '&') {
            if (buf_append(b, "&amp;", 5) != 0) return -1;
        } else {
            if (buf_append_char(b, c) != 0) return -1;
        }
    }
    return 0;
}

static int buf_append_escaped_text(FlatOutBuf* b, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '<') {
            if (buf_append(b, "&lt;", 4) != 0) return -1;
        } else if (c == '>') {
            if (buf_append(b, "&gt;", 4) != 0) return -1;
        } else if (c == '&') {
            if (buf_append(b, "&amp;", 5) != 0) return -1;
        } else {
            if (buf_append_char(b, c) != 0) return -1;
        }
    }
    return 0;
}

static int buf_append_indent(FlatOutBuf* b, int level, int indent) {
    if (indent <= 0) return 0;
    int total = level * indent;
    for (int i = 0; i < total; i++) {
        if (buf_append_char(b, ' ') != 0) return -1;
    }
    return 0;
}

/* Recursive element walker. */
static int flat_serialize_element(FlatDoc* flat, uint32_t idx,
                                   FlatOutBuf* b, int level, int indent) {
    if (idx == FLAT_INDEX_NULL) return 0;
    const FlatNode* n = &flat->nodes[idx];
    const char* xml = flat->xml_buffer;

    /* Newline + indent before opening tag (if indenting). */
    if (indent > 0 && level > 0) {
        if (buf_append_char(b, '\n') != 0) return -1;
        if (buf_append_indent(b, level, indent) != 0) return -1;
    }

    /* Opening tag. */
    if (buf_append_char(b, '<') != 0) return -1;
    if (buf_append(b, xml + n->name_offset, n->name_len) != 0) return -1;

    /* Attributes. */
    for (uint16_t i = 0; i < n->attr_count; i++) {
        const FlatAttr* a = &flat->attrs[n->attr_start + i];
        if (buf_append(b, " ", 1) != 0) return -1;
        if (buf_append(b, xml + a->name_offset, a->name_len) != 0) return -1;
        if (buf_append(b, "=\"", 2) != 0) return -1;
        if (buf_append_escaped_attr(b, xml + a->value_offset,
                                     a->value_len) != 0) return -1;
        if (buf_append_char(b, '"') != 0) return -1;
    }

    /* Walk children. */
    uint32_t child = n->first_child;
    int has_children = (child != FLAT_INDEX_NULL);

    if (!has_children) {
        /* Empty element: <name/>. */
        if (buf_append(b, "/>", 2) != 0) return -1;
        return 0;
    }

    /* Has children — emit `>` then walk. */
    if (buf_append_char(b, '>') != 0) return -1;

    /* Check if children are only one text node (inline). */
    int all_text_or_one = 1;
    for (uint32_t c = child; c != FLAT_INDEX_NULL;
         c = flat->nodes[c].next_sibling) {
        FlatNodeType t = (FlatNodeType)flat->nodes[c].type;
        if (t != FLAT_NODE_TEXT) {
            all_text_or_one = 0;
            break;
        }
    }
    int inline_children = all_text_or_one;

    /* Walk each child. */
    for (uint32_t c = child; c != FLAT_INDEX_NULL;
         c = flat->nodes[c].next_sibling) {
        const FlatNode* cn = &flat->nodes[c];
        FlatNodeType t = (FlatNodeType)cn->type;
        if (t == FLAT_NODE_ELEMENT) {
            int child_level = inline_children ? 0 : level + 1;
            if (flat_serialize_element(flat, c, b, child_level, indent) != 0)
                return -1;
        } else if (t == FLAT_NODE_TEXT) {
            uint32_t off = flat_node_text_offset(cn);
            uint32_t len = flat_node_text_len(cn);
            if (buf_append_escaped_text(b, xml + off, len) != 0) return -1;
        } else if (t == FLAT_NODE_COMMENT) {
            uint32_t off = flat_node_text_offset(cn);
            uint32_t len = flat_node_text_len(cn);
            if (indent > 0 && level > 0) {
                /* Inline comment within text — emit as-is. */
            }
            if (buf_append(b, "<!--", 4) != 0) return -1;
            if (buf_append(b, xml + off, len) != 0) return -1;
            if (buf_append(b, "-->", 3) != 0) return -1;
        } else if (t == FLAT_NODE_CDATA) {
            uint32_t off = flat_node_text_offset(cn);
            uint32_t len = flat_node_text_len(cn);
            if (buf_append(b, "<![CDATA[", 9) != 0) return -1;
            if (buf_append(b, xml + off, len) != 0) return -1;
            if (buf_append(b, "]]>", 3) != 0) return -1;
        } else if (t == FLAT_NODE_PI) {
            uint32_t data_off = flat_node_pi_data_offset(cn);
            uint32_t data_len = flat_node_pi_data_len(cn);
            if (buf_append(b, "<?", 2) != 0) return -1;
            if (buf_append(b, xml + cn->name_offset, cn->name_len) != 0) return -1;
            if (data_len > 0) {
                if (buf_append_char(b, ' ') != 0) return -1;
                if (buf_append(b, xml + data_off, data_len) != 0) return -1;
            }
            if (buf_append(b, "?>", 2) != 0) return -1;
        }
    }

    /* Closing tag. */
    if (indent > 0 && !inline_children) {
        if (buf_append_char(b, '\n') != 0) return -1;
        if (buf_append_indent(b, level, indent) != 0) return -1;
    }
    if (buf_append(b, "</", 2) != 0) return -1;
    if (buf_append(b, xml + n->name_offset, n->name_len) != 0) return -1;
    if (buf_append_char(b, '>') != 0) return -1;

    return 0;
}

char* flat_serialize_document(struct taurus_document* doc,
                               int xml_declaration,
                               int indent,
                               const char* encoding) {
    if (!doc || !doc->flat_doc) return NULL;
    FlatDoc* flat = doc->flat_doc;
    if (flat->root_index == FLAT_INDEX_NULL) return NULL;

    FlatOutBuf b = {NULL, 0, 0};

    /* Optional XML declaration. */
    if (xml_declaration) {
        const char* enc = encoding ? encoding : "UTF-8";
        char decl[128];
        int n = snprintf(decl, sizeof(decl),
                         "<?xml version=\"1.0\" encoding=\"%s\"?>", enc);
        if (n > 0 && (size_t)n < sizeof(decl)) {
            if (buf_append(&b, decl, (size_t)n) != 0) {
                free(b.data); return NULL;
            }
            if (indent > 0) {
                if (buf_append_char(&b, '\n') != 0) {
                    free(b.data); return NULL;
                }
            }
        }
    }

    /* Doc-level PIs (those before the root element). Walk the flat
     * array in order, emitting FLAT_NODE_PI nodes whose parent is
     * FLAT_INDEX_NULL. */
    for (size_t i = 0; i < flat->node_count; i++) {
        const FlatNode* fn = &flat->nodes[i];
        if ((FlatNodeType)fn->type != FLAT_NODE_PI) continue;
        if (fn->parent != FLAT_INDEX_NULL) continue;
        uint32_t data_off = flat_node_pi_data_offset(fn);
        uint32_t data_len = flat_node_pi_data_len(fn);
        if (buf_append(&b, "<?", 2) != 0) { free(b.data); return NULL; }
        if (buf_append(&b, flat->xml_buffer + fn->name_offset, fn->name_len) != 0) {
            free(b.data); return NULL;
        }
        if (data_len > 0) {
            if (buf_append_char(&b, ' ') != 0) { free(b.data); return NULL; }
            if (buf_append(&b, flat->xml_buffer + data_off, data_len) != 0) {
                free(b.data); return NULL;
            }
        }
        if (buf_append(&b, "?>", 2) != 0) { free(b.data); return NULL; }
        if (indent > 0) {
            if (buf_append_char(&b, '\n') != 0) { free(b.data); return NULL; }
        }
    }

    /* Root element. */
    if (flat_serialize_element(flat, flat->root_index, &b, 0, indent) != 0) {
        free(b.data);
        return NULL;
    }
    if (indent > 0) {
        if (buf_append_char(&b, '\n') != 0) {
            free(b.data); return NULL;
        }
    }

    return b.data;
}

char* flat_serialize_subtree(struct taurus_document* doc,
                              uint32_t root_flat_idx,
                              int indent) {
    if (!doc || !doc->flat_doc) return NULL;
    FlatDoc* flat = doc->flat_doc;
    if (root_flat_idx >= flat->node_count) return NULL;

    FlatOutBuf b = {NULL, 0, 0};
    if (flat_serialize_element(flat, root_flat_idx, &b, 0, indent) != 0) {
        free(b.data);
        return NULL;
    }
    return b.data;
}
