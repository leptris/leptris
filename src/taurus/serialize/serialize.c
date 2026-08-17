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
#include "../include/taurus.h"     /* TAURUS_API (visibility attribute) */
#include "../dom/node.h"            /* TaurusNodeVTable + taurus_node_vtable_for */
#include "../taurus_internal.h"
#include "../common/entities.h"
#include "../common/string_view.h"
/* TaurusSerializeOptions comes from taurus/types.h via taurus_internal.h's
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
    SerializeBuffer* buf = TAURUS_ALLOC(SerializeBuffer);
    if (!buf) return NULL;

    buf->capacity = INITIAL_BUFFER_CAPACITY;
    buf->data = TAURUS_ALLOC_N(char, buf->capacity);
    if (!buf->data) {
        TAURUS_FREE(buf);
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

    char* new_data = TAURUS_REALLOC_N(buf->data, char, new_cap);
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
    char* result = TAURUS_ALLOC_N(char, buf->size + 1);
    if (result) {
        memcpy(result, buf->data, buf->size + 1);
    }

    return result;
}

void buffer_free(SerializeBuffer* buf) {
    if (buf) {
        if (buf->data) {
            TAURUS_FREE(buf->data);
        }
        TAURUS_FREE(buf);
    }
}

int buffer_has_error(SerializeBuffer* buf) {
    return buf ? buf->alloc_failed : 1;
}

/* ============================================================================
 * XML Entity Escaping
 * ============================================================================ */

/* Escape special XML characters in text content.
 * Ordinary-character RUNS are bulk-appended (one capacity check +
 * one memcpy per run) instead of per-character appends — the
 * per-byte path was the serializer's dominant cost (TODO 194). */
static void buffer_append_escaped(SerializeBuffer* buf, const char* str) {
    if (!str) return;

    size_t run = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        const char* rep = NULL;
        size_t rep_len = 0;
        switch (str[i]) {
            case '<':  rep = "&lt;";   rep_len = 4; break;
            case '>':  rep = "&gt;";   rep_len = 4; break;
            case '&':  rep = "&amp;";  rep_len = 5; break;
            case '"':  rep = "&quot;"; rep_len = 6; break;
            case '\'': rep = "&apos;"; rep_len = 6; break;
            default:
                run++;
                continue;
        }
        if (run > 0) {
            buffer_append_len(buf, &str[i - run], run);
            run = 0;
        }
        buffer_append_len(buf, rep, rep_len);
    }
    if (run > 0) {
        buffer_append_len(buf, str, run);
    }
}

/* Escape attribute values (same as text but ensure quotes are escaped) */
static void buffer_append_attribute_value(SerializeBuffer* buf, const char* str) {
    buffer_append_escaped(buf, str);
}

/* ============================================================================
 * Node Serialization Functions
 * ============================================================================ */

void serialize_text_internal(TaurusTextNode* text, SerializeBuffer* buf) {
    if (!text) return;

    /* Materialize + expand entities on first access. For borrowed
     * text nodes (the direct_parse fast path), this expands
     * predefined entities (&amp; → &) and, when a DTD is present
     * on the document, custom entities (&foo; → declared value).
     * Without this call the serializer reads raw borrowed bytes
     * and emits unexpanded entity references. */
    const char* content = taurus_text_get_content(text);
    if (!content) return;
    size_t content_len = text->content_len;

    for (size_t i = 0; i < content_len; i++) {
        /* Check if this is start of entity reference */
        if (content[i] == '&') {
            /* Look ahead for ';' to detect entity reference */
            size_t j = i + 1;
            int found_semicolon = 0;

            /* Entity names are typically short (lt, gt, amp, quot, apos, or #digits) */
            while (j < text->content_len && j < i + 12) {
                if (content[j] == ';') {
                    found_semicolon = 1;
                    break;
                }
                /* Valid entity chars: alphanumeric, #, or - */
                if (!isalnum((unsigned char)content[j]) && content[j] != '#' && content[j] != '-') {
                    break;
                }
                j++;
            }

            if (found_semicolon && j > i + 1) {
                /* It's an entity reference - output as-is */
                buffer_append_len(buf, &content[i], j - i + 1);
                i = j;
                continue;
            }

            /* Not an entity reference - escape the bare & */
            buffer_append(buf, "&amp;");
            continue;
        }

        /* Normal escaping for other special characters */
        switch (content[i]) {
            case '<':
                buffer_append(buf, "&lt;");
                break;
            case '>':
                buffer_append(buf, "&gt;");
                break;
            /* Note: &quot; and &apos; are NOT escaped in text content
             * (they're only escaped in attribute values) */
            case '"':
            case '\'':
                buffer_append_char(buf, content[i]);
                break;
            default:
                buffer_append_char(buf, content[i]);
                break;
        }
    }
}

void serialize_comment_internal(TaurusCommentNode* comment, SerializeBuffer* buf) {
    if (!comment || !comment->content) return;

    buffer_append(buf, "<!--");
    buffer_append(buf, comment->content);
    buffer_append(buf, "-->");
}

void serialize_cdata_internal(TaurusCDATANode* cdata, SerializeBuffer* buf) {
    if (!cdata || !cdata->content) return;

    buffer_append(buf, "<![CDATA[");
    /* CRITICAL: NO escaping in CDATA - content is literal */
    buffer_append(buf, cdata->content);
    buffer_append(buf, "]]>");
}

void serialize_pi_internal(TaurusPINode* pi, SerializeBuffer* buf) {
    if (!pi || !pi->target) return;

    buffer_append(buf, "<?");
    buffer_append(buf, pi->target);

    if (pi->data && pi->data[0] != '\0') {
        buffer_append_char(buf, ' ');
        buffer_append(buf, pi->data);
    }

    buffer_append(buf, "?>");
}

void serialize_doctype_internal(TaurusDoctypeNode* doctype, SerializeBuffer* buf) {
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

void serialize_element_internal(TaurusElement elem, SerializeBuffer* buf, int is_root) {
    if (!elem) return;

    /* Get element name - use cached string if available, otherwise use StringView directly */
    const char* elem_name;
    size_t elem_name_len;

    if (elem->name) {
        /* Use cached NULL-terminated string */
        elem_name = elem->name;
        elem_name_len = strlen(elem->name);
    } else {
        return;
    }

    /* Add indentation before opening tag (not for root element) */
    if (!is_root && buf->indent_spaces > 0) {
        buffer_append_indent(buf);
    }

    /* Opening tag */
    buffer_append_char(buf, '<');
    buffer_append_len(buf, elem_name, elem_name_len);

    /* Attributes - iterate through linked list */
    for (struct taurus_attribute* attr = taurus_element_get_first_attribute(elem); attr != NULL; attr = taurus_attr_next(attr)) {
        if (!attr) continue;

        /* Expand entity-containing values lazily before re-escaping
         * (single representation, round 4: the expanded copy REPLACES
         * the view so the raw '&' doesn't double-escape). */
        const char* val;
        if (attr->has_entities) {
            struct taurus_document* doc = taurus_element_get_document(elem);
            TaurusMemoryPool* pool = doc ? doc->pool : NULL;
            char* resolved = pool
                ? taurus_decode_entities_view(&attr->value_view, pool)
                : NULL;
            if (resolved) {
                attr->value_view = taurus_sv_from_cstr(resolved);
                attr->has_entities = 0;
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
        size_t val_len = strlen(val);
        size_t needed = 1 + name_len + 2 + 6 * val_len + 2;
        buffer_ensure_capacity(buf, needed + 1);

        char* out = buf->data + buf->size;
        *out++ = ' ';
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

    /* Namespaces - serialize as xmlns attributes */
    for (struct taurus_namespace* ns = taurus_elem_namespaces(elem); ns != NULL; ns = ns->next) {
        if (!ns) continue;

        buffer_append_char(buf, ' ');
        buffer_append(buf, "xmlns");

        /* Add prefix if not default namespace */
        if (ns->prefix) {
            buffer_append_char(buf, ':');
            buffer_append(buf, ns->prefix);
        }

        buffer_append(buf, "=\"");
        buffer_append(buf, ns->uri ? ns->uri : "");
        buffer_append_char(buf, '"');
    }

    /* Check if element has children */
    if (taurus_node_first_child_internal((TaurusNode*)elem)) {
        /* Check if this is a text-only element (single text child, no element children) */
        int is_text_only = 1;
        TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)elem);

        /* Check if there's only one child and it's a text node */
        if (child && child->type == TAURUS_NODE_TYPE_TEXT) {
            /* Check if there are any siblings (more children) */
            if (taurus_node_get_next_sibling(child) != NULL) {
                is_text_only = 0;
            }
        } else {
            /* Not a single text node */
            is_text_only = 0;
        }

        if (is_text_only && buf->indent_spaces == 0) {
            /* Text-only element in compact mode - serialize inline */
            /* Close opening tag */
            buffer_append_char(buf, '>');

            /* Serialize the single text child */
            serialize_node_internal(taurus_node_first_child_internal((TaurusNode*)elem), buf);

            /* Closing tag */
            buffer_append(buf, "</");
            buffer_append_len(buf, elem_name, elem_name_len);
            buffer_append_char(buf, '>');
        } else if (is_text_only && buf->indent_spaces > 0) {
            /* Text-only element with indenting - serialize with newlines */
            /* Close opening tag */
            buffer_append_char(buf, '>');

            /* Serialize the single text child */
            serialize_node_internal(taurus_node_first_child_internal((TaurusNode*)elem), buf);

            /* Closing tag */
            buffer_append(buf, "</");
            buffer_append_len(buf, elem_name, elem_name_len);
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
            TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)elem);
            while (child) {
                /* Pass is_root=0 for all children */
                if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                    serialize_element_internal((TaurusElement)child, buf, 0);
                } else {
                    serialize_node_internal(child, buf);
                }
                child = taurus_node_get_next_sibling(child);
            }

            /* Decrease indent level after children */
            buf->indent--;

            /* Add indentation before closing tag */
            if (buf->indent_spaces > 0) {
                buffer_append_indent(buf);
            }

            /* Closing tag */
            buffer_append(buf, "</");
            buffer_append_len(buf, elem_name, elem_name_len);
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

void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    if (!node) return;

    /* Dispatch via the per-type vtable registry — adding a new node
     * type is purely additive (register a vtable in node_vtable.c). */
    const TaurusNodeVTable* vt = taurus_node_vtable_for(node->type);
    if (vt && vt->serialize) {
        vt->serialize(node, buf);
    }
    /* Unknown/unregistered type — silently skip (matches old default). */
}

/* ============================================================================
 * Public API
 * ============================================================================ */

char* taurus_serialize_node(TaurusNode* node) {
    if (!node) return NULL;

    SerializeBuffer* buf = buffer_create(0);  /* Compact mode */
    if (!buf) return NULL;

    serialize_node_internal(node, buf);

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

char* taurus_serialize_element(TaurusElement elem) {
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

char* taurus_serialize_document_with_declaration(TaurusElement root,
                                                   const char* encoding,
                                                   const char* version,
                                                   int standalone,
                                                   int has_bom,
                                                   TaurusDoctypeNode* doctype) {
    if (!root) return NULL;

    SerializeBuffer* buf = buffer_create(0);  /* Compact mode by default */
    if (!buf) return NULL;

    /* Output UTF-8 BOM if present in original */
    if (has_bom) {
        buffer_append_char(buf, (char)0xEF);
        buffer_append_char(buf, (char)0xBB);
        buffer_append_char(buf, (char)0xBF);
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
 * Document Serialization (matches taurus.h public API)
 * ============================================================================ */

/* Forward declarations for document structure access */
struct taurus_document;

/* Serialize document with options */
TAURUS_API char* taurus_document_serialize(struct taurus_document* doc,
                                 TaurusSerializeOptions* options) {
    if (!doc) return NULL;

    /* FlatDoc serialize fast path removed — direct_parse builds the
     * TaurusElement tree eagerly. Serialization always walks the DOM. */

    /* Get root element from new_dom_root field */
    /* TODO 139 Phase D: trigger lazy promote if the doc was produced
     * by the flat-parse fast path. */
    taurus_document_ensure_promoted(doc);
    TaurusElement root = (TaurusElement)doc->new_dom_root;
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
        buffer_append_char(buf, (char)0xEF);
        buffer_append_char(buf, (char)0xBB);
        buffer_append_char(buf, (char)0xBF);
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
        serialize_doctype_internal((TaurusDoctypeNode*)doc->doctype, buf);
        if (indent_spaces > 0) {
            buffer_append_newline(buf);
        }
    }

    /* Output document-level processing instructions.  These are PIs
     * that appeared before or after the root element in the original
     * document (e.g. <?xml-stylesheet?>).  Order is preserved by the
     * parser appending to a linked list. */
    for (struct taurus_processing_instruction* pi = doc->pis;
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
 * Element Serialization (matches taurus.h public API)
 * ============================================================================ */

/* Serialize element subtree to XML string */
TAURUS_API char* taurus_element_serialize(TaurusElement elem,
                                 TaurusSerializeOptions* options) {
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
TAURUS_API int taurus_document_save_file(struct taurus_document* doc,
                               const char* filepath,
                               TaurusSerializeOptions* options) {
    if (!doc) return -4;  /* TAURUS_ERROR_NULL_ARG */
    if (!filepath) return -4;  /* TAURUS_ERROR_NULL_ARG */

    /* Serialize document to XML string */
    char* xml = taurus_document_serialize(doc, options);
    if (!xml) return -1;  /* TAURUS_ERROR_MEMORY */

    /* Open file for writing */
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        TAURUS_FREE(xml);
        return -7;  /* TAURUS_ERROR_IO */
    }

    /* Write XML content to file */
    size_t len = strlen(xml);
    size_t written = fwrite(xml, 1, len, file);
    fclose(file);

    /* Free the XML string */
    TAURUS_FREE(xml);

    /* Check if all bytes were written */
    if (written != len) {
        return -7;  /* TAURUS_ERROR_IO */
    }

    return 0;  /* TAURUS_OK */
}
