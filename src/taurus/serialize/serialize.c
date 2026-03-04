/* lib/src/serialize/serialize.c - XML Serialization Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * CRITICAL PRINCIPLES:
 * 1. Character-perfect output - must match input exactly
 * 2. No escaping in CDATA - CDATA content is literal
 * 3. Proper escaping elsewhere - text needs <>&"' escaped
 * 4. Document order - traverse children in correct order
 *
 * POINTER-BASED ARCHITECTURE: Uses ptr_element directly.
 */

#include "serialize.h"
#include "../taurus_internal.h"
#include "../../include/taurus.h"  /* For taurus_element_attribute() and TaurusSerializeOptions */
#include "../dom/ptr_element.h"   /* For ptr_element and ptr_attribute structures */
#include "../dom/ptr_accessor.h"  /* For ptr_element accessors */
#include "../common/entities.h" /* For taurus_decode_entities() */
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Initial buffer capacity */
#define INITIAL_BUFFER_CAPACITY 4096  /* Increased from 1024 for better performance */

/* Growth factor for buffer - use 1.5x instead of 2x for better memory efficiency */
#define BUFFER_GROWTH_FACTOR_NUM 3
#define BUFFER_GROWTH_FACTOR_DEN 2

/* ============================================================================
 * Size Calculation (Pass 1 of Two-Pass Serialization)
 * ============================================================================
 *
 * These functions calculate the exact serialized size without allocating
 * or writing anything. This enables pre-allocated buffer for large documents,
 * eliminating realloc overhead.
 */

/* Calculate escaped length of a string (worst case: every char becomes &lt; etc) */
static size_t calc_escaped_length(const char* str) {
    if (!str) return 0;

    size_t len = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        switch (str[i]) {
            case '<':  len += 4; break;  /* &lt; */
            case '>':  len += 4; break;  /* &gt; */
            case '&':  len += 5; break;  /* &amp; */
            case '"':  len += 6; break;  /* &quot; */
            case '\'': len += 6; break;  /* &apos; */
            default:   len += 1; break;
        }
    }
    return len;
}

/* Calculate text node serialized length (with entity detection) */
static size_t calc_text_length(TaurusTextNode* text) {
    if (!text || !text->content) return 0;

    const char* content = text->content;
    size_t len = 0;

    for (size_t i = 0; content[i] != '\0'; i++) {
        if (content[i] == '&') {
            /* Look ahead for entity reference */
            size_t j = i + 1;
            int found_semicolon = 0;
            while (content[j] && j < i + 12) {
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
                len += j - i + 1;  /* Entity reference as-is */
                i = j;
            } else {
                len += 5;  /* &amp; */
            }
        } else if (content[i] == '<') {
            len += 4;  /* &lt; */
        } else if (content[i] == '>') {
            len += 4;  /* &gt; */
        } else {
            len += 1;
        }
    }
    return len;
}

/* Forward declaration for recursive size calculation */
static size_t calc_element_size(TaurusElement elem, int indent_spaces, int indent_level, int is_root);

/* Calculate node serialized size */
static size_t calc_node_size(TaurusNode* node, int indent_spaces, int indent_level) {
    if (!node) return 0;

    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT:
            return calc_element_size((TaurusElement)node, indent_spaces, indent_level, 0);

        case TAURUS_NODE_TYPE_TEXT:
            return calc_text_length((TaurusTextNode*)node);

        case TAURUS_NODE_TYPE_COMMENT: {
            TaurusCommentNode* comment = (TaurusCommentNode*)node;
            return 4 + (comment->content ? strlen(comment->content) : 0) + 3;  /* <!-- --> */
        }

        case TAURUS_NODE_TYPE_CDATA: {
            TaurusCDATANode* cdata = (TaurusCDATANode*)node;
            return 9 + (cdata->content ? strlen(cdata->content) : 0) + 3;  /* <![CDATA[]]> */
        }

        case TAURUS_NODE_TYPE_PI: {
            TaurusPINode* pi = (TaurusPINode*)node;
            size_t len = 2 + (pi->target ? strlen(pi->target) : 0);  /* <?target */
            if (pi->data && pi->data[0]) {
                len += 1 + strlen(pi->data);  /* data */
            }
            return len + 2;  /* ?> */
        }

        case TAURUS_NODE_TYPE_DOCTYPE: {
            TaurusDoctypeNode* doctype = (TaurusDoctypeNode*)node;
            size_t len = 10 + (doctype->name ? strlen(doctype->name) : 0);  /* <!DOCTYPE name */
            if (doctype->public_id) {
                len += 9 + strlen(doctype->public_id);  /* PUBLIC "..." */
                if (doctype->system_id) {
                    len += 3 + strlen(doctype->system_id);  /* "..." */
                }
            } else if (doctype->system_id) {
                len += 9 + strlen(doctype->system_id);  /* SYSTEM "..." */
            }
            if (doctype->internal_subset) {
                len += 2 + strlen(doctype->internal_subset);  /* [...] */
            }
            return len + 1;  /* > */
        }

        default:
            return 0;
    }
}

/* Calculate element serialized size */
static size_t calc_element_size(TaurusElement elem, int indent_spaces, int indent_level, int is_root) {
    if (!elem) return 0;

    size_t size = 0;

    /* Indentation before opening tag */
    if (!is_root && indent_spaces > 0) {
        size += (size_t)(indent_level * indent_spaces);
    }

    /* Opening tag: <name */
    size += 1 + (elem->name ? strlen(elem->name) : 0);

    /* Attributes */
    for (struct ptr_attribute* attr = elem->first_attr; attr != NULL; attr = attr->next_attr) {
        if (!attr || !attr->name) continue;
        size += 1;  /* space */
        size += strlen(attr->name);
        size += 2;  /* =" */
        size += calc_escaped_length(attr->value ? attr->value : "");
        size += 1;  /* " */
    }

    /* Namespaces - stored as xmlns:prefix attributes */
    for (struct ptr_attribute* attr = elem->first_attr; attr != NULL; attr = attr->next_attr) {
        if (!attr || !attr->name) continue;
        /* Check if this is a namespace declaration */
        if (strcmp(attr->name, "xmlns") == 0 || strncmp(attr->name, "xmlns:", 6) == 0) {
            /* Already counted as attribute above */
        }
    }

    /* Children or self-closing */
    if (elem->first_child) {
        size += 1;  /* > */

        /* Newline after opening tag if indenting */
        if (indent_spaces > 0) {
            size += 1;  /* \n */
        }

        /* Children */
        TaurusNode* child = elem->first_child;
        while (child) {
            size += calc_node_size(child, indent_spaces, indent_level + 1);
            child = child->next_sibling;
        }

        /* Indentation before closing tag */
        if (indent_spaces > 0) {
            size += (size_t)(indent_level * indent_spaces);
        }

        /* Closing tag: </name> */
        size += 2 + (elem->name ? strlen(elem->name) : 0) + 1;

        /* Newline after closing tag if not root */
        if (!is_root && indent_spaces > 0) {
            size += 1;  /* \n */
        }
    } else {
        /* Self-closing: /> */
        size += 2;

        /* Newline after self-closing if not root */
        if (!is_root && indent_spaces > 0) {
            size += 1;  /* \n */
        }
    }

    return size;
}

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
    buf->preserve_whitespace = 0;  /* Default: allow whitespace normalization */
    buf->indent_char = INDENT_CHAR_SPACE;  /* Default: spaces */
    buf->line_ending = LINE_ENDING_LF;     /* Default: Unix line endings */

    return buf;
}

SerializeBuffer* buffer_create_with_options(int indent_spaces, IndentChar indent_char, LineEnding line_ending) {
    SerializeBuffer* buf = buffer_create(indent_spaces);
    if (!buf) return NULL;

    buf->indent_char = indent_char;
    buf->line_ending = line_ending;

    return buf;
}

void buffer_ensure_capacity(SerializeBuffer* buf, size_t needed) {
    if (buf->size + needed < buf->capacity) return;

    /* Use 1.5x growth factor (industry standard) instead of 2x */
    size_t new_capacity = buf->capacity + buf->capacity / 2;
    while (buf->size + needed >= new_capacity) {
        new_capacity += new_capacity / 2;
    }

    char* new_data = TAURUS_REALLOC_N(buf->data, char, new_capacity);
    if (new_data) {
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
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
    /* Don't add indentation when xml:space="preserve" is in effect */
    if (buf->preserve_whitespace) return;

    int indent_count = buf->indent * buf->indent_spaces;
    buffer_ensure_capacity(buf, indent_count + 1);

    if (buf->indent_char == INDENT_CHAR_TAB) {
        /* Use tabs for indentation */
        for (int i = 0; i < indent_count; i++) {
            buf->data[buf->size++] = '\t';
        }
    } else {
        /* Use spaces for indentation (default) */
        for (int i = 0; i < indent_count; i++) {
            buf->data[buf->size++] = ' ';
        }
    }
    buf->data[buf->size] = '\0';
}

void buffer_append_newline(SerializeBuffer* buf) {
    if (!buf || buf->indent_spaces <= 0) return;
    /* Don't add newlines when xml:space="preserve" is in effect */
    if (buf->preserve_whitespace) return;

    if (buf->line_ending == LINE_ENDING_CRLF) {
        /* Windows line ending */
        buffer_append_char(buf, '\r');
        buffer_append_char(buf, '\n');
    } else {
        /* Unix line ending (default) */
        buffer_append_char(buf, '\n');
    }
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

/* ============================================================================
 * XML Entity Escaping
 * ============================================================================ */

/* Escape special XML characters in text content */
static void buffer_append_escaped(SerializeBuffer* buf, const char* str) {
    if (!str) return;

    /* BLOCK COPY OPTIMIZATION: Copy runs of safe characters in bulk */
    size_t i = 0;
    while (str[i] != '\0') {
        /* Find start of safe run */
        size_t run_start = i;
        size_t run_len = 0;

        /* Scan for safe characters (no escaping needed) */
        while (str[i] != '\0') {
            char c = str[i];
            if (c == '<' || c == '>' || c == '&' || c == '"' || c == '\'') {
                break;
            }
            run_len++;
            i++;
        }

        /* Copy the safe run in one block */
        if (run_len > 0) {
            buffer_append_len(buf, &str[run_start], run_len);
        }

        /* Handle special characters that need escaping */
        switch (str[i]) {
            case '<':
                buffer_append(buf, "&lt;");
                i++;
                break;
            case '>':
                buffer_append(buf, "&gt;");
                i++;
                break;
            case '&':
                buffer_append(buf, "&amp;");
                i++;
                break;
            case '"':
                buffer_append(buf, "&quot;");
                i++;
                break;
            case '\'':
                buffer_append(buf, "&apos;");
                i++;
                break;
        }
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
    if (!text || !text->content) return;

    const char* content = text->content;

    /* BLOCK COPY OPTIMIZATION: Find runs of safe characters and copy in bulk
     * This avoids per-character function call overhead for the common case
     * where most text doesn't need escaping. */
    size_t i = 0;
    while (content[i] != '\0') {
        /* Find start of run */
        size_t run_start = i;
        size_t run_len = 0;

        /* Scan for safe characters (no escaping needed) */
        while (content[i] != '\0') {
            char c = content[i];
            if (c == '<' || c == '>' || c == '&') {
                break;
            }
            run_len++;
            i++;
        }

        /* Copy the safe run in one block */
        if (run_len > 0) {
            buffer_append_len(buf, &content[run_start], run_len);
        }

        /* Handle special characters that need escaping */
        if (content[i] == '&') {
            /* Look ahead for ';' to detect entity reference */
            size_t j = i + 1;
            int found_semicolon = 0;

            /* Entity names are typically short (lt, gt, amp, quot, apos, or #digits) */
            while (content[j] && j < i + 12) {
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
                i = j + 1;
            } else {
                /* Not an entity reference - escape the bare & */
                buffer_append(buf, "&amp;");
                i++;
            }
        } else if (content[i] == '<') {
            buffer_append(buf, "&lt;");
            i++;
        } else if (content[i] == '>') {
            buffer_append(buf, "&gt;");
            i++;
        }
        /* Note: &quot; and &apos; are NOT escaped in text content */
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

    /* Get document for compact mode - COMPACT-ONLY */
    struct taurus_document* doc = elem->document;

    /* Get element name using public API which handles compact mode */
    const char* elem_name = taurus_element_name(elem);
    if (!elem_name || !*elem_name) return;  /* No name available */
    size_t elem_name_len = strlen(elem_name);

    /* Add indentation before opening tag (not for root element) */
    if (!is_root && buf->indent_spaces > 0) {
        buffer_append_indent(buf);
    }

    /* Opening tag */
    buffer_append_char(buf, '<');
    buffer_append_len(buf, elem_name, elem_name_len);

    /* Attributes - use ptr_attribute list */
    if (elem->first_attr) {
        /* Use ptr_attribute list */
        for (struct ptr_attribute* attr = elem->first_attr; attr != NULL; attr = attr->next_attr) {
            /* Get name */
            const char* attr_name = attr->name;

            /* Get value */
            const char* attr_value = attr->value;

            if (attr_name && *attr_name) {
                buffer_append_char(buf, ' ');
                buffer_append(buf, attr_name);
                buffer_append(buf, "=\"");
                buffer_append_attribute_value(buf, attr_value ? attr_value : "");
                buffer_append_char(buf, '"');
            }
        }
    }

    /* Handle xml:space attribute for whitespace preservation */
    int old_preserve_whitespace = buf->preserve_whitespace;
    const char* space_attr = taurus_element_attribute(elem, "xml:space");
    if (space_attr) {
        if (strcmp(space_attr, "preserve") == 0) {
            buf->preserve_whitespace = 1;
        } else if (strcmp(space_attr, "default") == 0) {
            buf->preserve_whitespace = 0;
        }
    }

    /* Check if element has children (POINTER-ONLY) */
    int has_children = (elem->first_child != NULL);

    if (has_children) {
        /* Close opening tag */
        buffer_append_char(buf, '>');

        /* Check if element has wrapper children (newly created elements) */
        if (elem->first_child) {
            /* Serialize wrapper's children (for newly created/modified elements) */
            /* Add newline after opening tag if indenting */
            if (buf->indent_spaces > 0) {
                buffer_append_newline(buf);
            }

            /* Increase indent level */
            buf->indent++;

            /* Serialize all children from wrapper's linked list */
            TaurusNode* child = elem->first_child;
            while (child) {
                serialize_node_internal(child, buf);
                /* Get next sibling - use base.next_sibling which is set during wrapper creation */
                child = child->next_sibling;
            }

            /* Decrease indent level */
            buf->indent--;

            /* Indentation before closing tag */
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

    /* Restore previous xml:space state */
    buf->preserve_whitespace = old_preserve_whitespace;
}

void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    if (!node) return;

    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT:
            serialize_element_internal((TaurusElement)node, buf, 0);
            break;

        case TAURUS_NODE_TYPE_TEXT:
            serialize_text_internal((TaurusTextNode*)node, buf);
            break;

        case TAURUS_NODE_TYPE_COMMENT:
            serialize_comment_internal((TaurusCommentNode*)node, buf);
            break;

        case TAURUS_NODE_TYPE_CDATA:
            serialize_cdata_internal((TaurusCDATANode*)node, buf);
            break;

        case TAURUS_NODE_TYPE_PI:
            serialize_pi_internal((TaurusPINode*)node, buf);
            break;

        case TAURUS_NODE_TYPE_DOCTYPE:
            serialize_doctype_internal((TaurusDoctypeNode*)node, buf);
            break;

        default:
            /* Unknown node type - skip */
            break;
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

char* taurus_serialize_node(TaurusNode* node) {
    if (!node) return NULL;

    SerializeBuffer* buf = buffer_create(0);
    if (!buf) return NULL;

    serialize_node_internal(node, buf);

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

char* taurus_serialize_element(TaurusElement elem) {
    if (!elem) return NULL;

    SerializeBuffer* buf = buffer_create(0);
    if (!buf) return NULL;

    serialize_element_internal(elem, buf, 1);

    char* result = buffer_to_string(buf);
    buffer_free(buf);

    return result;
}

char* taurus_serialize_element_with_options(TaurusElement elem,
                                             int indent_spaces,
                                             IndentChar indent_char,
                                             LineEnding line_ending) {
    if (!elem) return NULL;

    SerializeBuffer* buf = buffer_create_with_options(indent_spaces, indent_char, line_ending);
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
char* taurus_document_serialize(struct taurus_document* doc,
                                 TaurusSerializeOptions* options) {
    if (!doc) return NULL;

    /* Get root element - use public API which handles compact mode */
    TaurusElement root = taurus_document_root(doc);
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

    /* If xml_declaration is explicitly requested but doc has no version, use defaults */
    int use_default_declaration = 0;
    if (xml_declaration && !xml_version) {
        xml_version = "1.0";
        standalone = -1;  /* No standalone declaration */
        use_default_declaration = 1;
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
char* taurus_element_serialize(TaurusElement elem,
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
int taurus_document_save_file(struct taurus_document* doc,
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
