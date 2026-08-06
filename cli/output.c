/**
 * @file output.c
 * @brief Output formatting implementation for Taurus CLI
 */

#include "output.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Need access to internal structures for formatters */
#include "../src/taurus/taurus_internal.h"
#include "../src/taurus/dom/element.h"  /* For TaurusElement and API */
#include "../src/taurus/dom/text.h"    /* For TaurusTextNode */
#include "../src/taurus/dom/cdata.h"   /* For TaurusCDATANode */
#include "../src/taurus/dom/comment.h" /* For TaurusCommentNode */
#include "../src/taurus/dom/pi.h"      /* For TaurusPINode */

/* ------------------------------------------------------------------------- */
/* Format Conversion                                                         */
/* ------------------------------------------------------------------------- */

output_format_t output_format_from_string(const char* format_str) {
    if (!format_str) return OUTPUT_FORMAT_XML;

    if (strcmp(format_str, "xml") == 0) return OUTPUT_FORMAT_XML;
    if (strcmp(format_str, "json") == 0) return OUTPUT_FORMAT_JSON;
    if (strcmp(format_str, "text") == 0) return OUTPUT_FORMAT_TEXT;

    return OUTPUT_FORMAT_XML;
}

const char* output_format_to_string(output_format_t format) {
    switch (format) {
        case OUTPUT_FORMAT_XML:  return "xml";
        case OUTPUT_FORMAT_JSON: return "json";
        case OUTPUT_FORMAT_TEXT: return "text";
        default:                 return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* XML Formatter Implementation                                              */
/* ------------------------------------------------------------------------- */

typedef struct xml_formatter_context {
    xml_format_options_t options;
} xml_formatter_context_t;

static void xml_print_indent(int level, int indent, FILE* out) {
    for (int i = 0; i < level * indent; i++) {
        fputc(' ', out);
    }
}

static void xml_print_element_recursive(
    TaurusElement elem,
    FILE* out,
    int level,
    xml_formatter_context_t* ctx
) {
    if (!elem || !out) return;

    /* Indent */
    if (ctx->options.pretty_print) {
        xml_print_indent(level, ctx->options.indent, out);
    }

    /* Opening tag - use API to get name */
    const char* name = taurus_element_get_name(elem);
    fprintf(out, "<%s", name ? name : "");

    /* Attributes - iterate using compact accessor functions */
    size_t attr_count = taurus_element_attribute_count(elem);
    for (size_t i = 0; i < attr_count; i++) {
        /* Get attribute by index using accessor function */
        const char* attr_name = NULL;
        const char* attr_value = NULL;

        /* Use API to get attribute name and value by index */
        /* Walk the attribute linked list manually to get the i-th attribute */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
        for (size_t j = 0; j < i && attr; j++) {
            attr = attr->next;
        }
        if (!attr) continue;

        /* Get attribute name (handle both StringView and C string) */
        attr_name = attr->name;
        char* temp_name = NULL;
        if (!attr_name && !taurus_sv_is_empty(&attr->name_view)) {
            /* Convert StringView to C string for output */
            temp_name = taurus_sv_to_cstr(&attr->name_view);
            attr_name = temp_name;
        }

        /* Get attribute value (handle both StringView and C string) */
        attr_value = attr->value;
        char* temp_value = NULL;
        if (!attr_value && !taurus_sv_is_empty(&attr->value_view)) {
            /* Convert StringView to C string for output */
            temp_value = taurus_sv_to_cstr(&attr->value_view);
            attr_value = temp_value;
        }

        if (attr_name) {
            fprintf(out, " %s=\"", attr_name);
            if (attr_value) {
                /* Escape XML entities */
                for (const char* p = attr_value; *p; p++) {
                    switch (*p) {
                        case '<':  fprintf(out, "&lt;"); break;
                        case '>':  fprintf(out, "&gt;"); break;
                        case '&':  fprintf(out, "&amp;"); break;
                        case '"':  fprintf(out, "&quot;"); break;
                        case '\'': fprintf(out, "&apos;"); break;
                        default:   fputc(*p, out); break;
                    }
                }
            }
            fprintf(out, "\"");
        }

        /* Clean up temporary conversions */
        if (temp_name) free(temp_name);
        if (temp_value) free(temp_value);
    }

    /* Namespace declarations - output xmlns attributes if element has namespace */
    const char* ns_uri = taurus_element_get_namespace_uri(elem);
    const char* ns_prefix = taurus_element_get_prefix(elem);
    if (ns_uri) {
        fprintf(out, " xmlns");
        if (ns_prefix) {
            fprintf(out, ":%s", ns_prefix);
        }
        fprintf(out, "=\"%s\"", ns_uri);
    }

    /* Namespace declarations stored on this element (xmlns:prefix="uri") */
    struct taurus_namespace* ns = elem->namespaces;
    while (ns) {
        fprintf(out, " xmlns");
        if (ns->prefix) {
            fprintf(out, ":%s", ns->prefix);
        }
        fprintf(out, "=\"%s\"", ns->uri);
        ns = ns->next;
    }

    /* Check if element has children (including text nodes) */
    TaurusNode* first_child = taurus_elem_first_child(elem);
    int has_children = (first_child != NULL);
    int first_is_element = first_child && TAURUS_NODE_IS_ELEMENT(first_child);

    if (!has_children) {
        /* Self-closing tag */
        fprintf(out, "/>");
        if (ctx->options.pretty_print) {
            fprintf(out, "\n");
        }
        return;
    }

    /* Close opening tag */
    fprintf(out, ">");

    /* Add newline after opening tag if pretty print and first child is element */
    if (ctx->options.pretty_print && first_is_element) {
        fprintf(out, "\n");
    }

    /* Children - iterate through ALL children (including text nodes) */
    TaurusNode* child = taurus_elem_first_child(elem);
    while (child) {
        if (TAURUS_NODE_IS_ELEMENT(child)) {
            /* Element child - recurse with indentation */
            xml_print_element_recursive((TaurusElement)child, out, level + 1, ctx);
        }
        else if (TAURUS_NODE_IS_TEXT(child)) {
            /* Text node - escape and print */
            TaurusTextNode* text_node = TAURUS_NODE_AS_TEXT(child);
            /* text may be borrowed (non-NUL-terminated); materialize so the
             * whitespace check and output loop below can rely on NUL. */
            const char* content = taurus_text_get_content(text_node);
            if (text_node && content) {
                /* In compact mode, skip whitespace-only text nodes */
                if (!ctx->options.pretty_print) {
                    /* Check if content is whitespace-only */
                    const char* p = content;
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                        p++;
                    }
                    if (*p == '\0') {
                        /* Whitespace-only, skip in compact mode */
                        child = taurus_node_get_next_sibling(child);
                        continue;
                    }
                }
                for (const char* p = content; *p; p++) {
                    switch (*p) {
                        case '<':  fprintf(out, "&lt;"); break;
                        case '>':  fprintf(out, "&gt;"); break;
                        case '&':  fprintf(out, "&amp;"); break;
                        default:   fputc(*p, out); break;
                    }
                }
            }
            /* Get next sibling via the type-dispatching accessor */
            child = taurus_node_get_next_sibling(child);
            continue;
        }
        else if (TAURUS_NODE_IS_CDATA(child)) {
            /* CDATA node - print as <![CDATA[...]]> */
            TaurusCDATANode* cdata = TAURUS_NODE_AS_CDATA(child);
            if (cdata && cdata->content) {
                fprintf(out, "<![CDATA[%s]]>", cdata->content);
            }
            child = taurus_node_get_next_sibling(child);
            continue;
        }
        else if (TAURUS_NODE_IS_COMMENT(child)) {
            /* Comment node - print as <!--...--> */
            TaurusCommentNode* comment = TAURUS_NODE_AS_COMMENT(child);
            if (comment && comment->content) {
                fprintf(out, "<!--%s-->", comment->content);
            }
            child = taurus_node_get_next_sibling(child);
            continue;
        }
        else if (TAURUS_NODE_IS_PI(child)) {
            /* Processing Instruction - print as <?target data?> */
            TaurusPINode* pi = TAURUS_NODE_AS_PI(child);
            if (pi && pi->target) {
                fprintf(out, "<?%s", pi->target);
                if (pi->data) {
                    fprintf(out, " %s", pi->data);
                }
                fprintf(out, "?>");
            }
            child = taurus_node_get_next_sibling(child);
            continue;
        }
        /* DOCTYPE nodes are not printed as children of elements */

        /* For element nodes, get next sibling using node API */
        child = taurus_node_get_next_sibling(child);
    }

    /* Closing tag */
    if (has_children && ctx->options.pretty_print && first_is_element) {
        xml_print_indent(level, ctx->options.indent, out);
    }
    fprintf(out, "</%s>", name ? name : "");
    if (ctx->options.pretty_print) {
        fprintf(out, "\n");
    }
}

static void xml_print_document_impl(
    struct taurus_document* doc,
    FILE* out,
    void* ctx
) {
    if (!doc || !out) return;

    xml_formatter_context_t* xml_ctx = (xml_formatter_context_t*)ctx;

    /* XML declaration */
    if (xml_ctx && xml_ctx->options.include_declaration) {
        fprintf(out, "<?xml version=\"1.0\" encoding=\"%s\"?>\n",
                xml_ctx->options.encoding);
    }

    /* Root element - use compact accessor */
    if (doc->new_dom_root) {
        TaurusElement root = (TaurusElement)doc->new_dom_root;
        xml_print_element_recursive(root, out, 0, xml_ctx);
    }
}

static void xml_print_element_impl(
    TaurusElement elem,
    FILE* out,
    void* ctx
) {
    if (!elem || !out) return;

    xml_formatter_context_t* xml_ctx = (xml_formatter_context_t*)ctx;
    xml_print_element_recursive(elem, out, 0, xml_ctx);
}

static void xml_print_nodeset_impl(
    struct taurus_xpath_result* result,
    FILE* out,
    void* ctx
) {
    if (!result || !out) return;
    if (result->type != XPATH_RESULT_NODESET) return;

    xml_formatter_context_t* xml_ctx = (xml_formatter_context_t*)ctx;
    XPathNodeSet* nodeset = result->value.nodeset_value;

    if (!nodeset) return;

    /* Print each node in the nodeset based on type */
    for (size_t i = 0; i < nodeset->count; i++) {
        void* node_ptr = nodeset->nodes[i];

        /* Check node type - attribute nodes have different structure than elements */
        if (IS_ATTRIBUTE_NODE(node_ptr)) {
            /* Attribute node - print as name="value" format */
            TaurusAttributeNode* attr = (TaurusAttributeNode*)node_ptr;
            if (attr && attr->name && attr->name[0] != '\0') {
                fprintf(out, "%s", attr->name);
                if (attr->value) {
                    fprintf(out, "=\"%s\"", attr->value);
                }
                fprintf(out, "\n");
            }
        } else if (IS_TEXT_NODE(node_ptr)) {
            /* Text node - print just the text content */
            XPathTextNode* text = (XPathTextNode*)node_ptr;
            if (text && text->content && text->content[0] != '\0') {
                fprintf(out, "%s\n", text->content);
            }
        } else if (IS_ELEMENT_NODE(node_ptr)) {
            /* Element node - use existing recursive print function */
            TaurusElement elem = (TaurusElement)node_ptr;
            xml_print_element_recursive(elem, out, 0, xml_ctx);
        } else {
            /* Unknown node type - skip */
            continue;
        }
    }
}

static void xml_print_string_impl(
    const char* str,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (str && out) {
        fprintf(out, "%s\n", str);
    }
}

static void xml_print_number_impl(
    double num,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (out) {
        fprintf(out, "%g\n", num);
    }
}

static void xml_print_boolean_impl(
    int value,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (out) {
        fprintf(out, "%s\n", value ? "true" : "false");
    }
}

static void xml_print_error_impl(
    const char* msg,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (msg && out) {
        fprintf(out, "error: %s\n", msg);
    }
}

static void xml_print_success_impl(
    const char* msg,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (msg && out) {
        fprintf(out, "%s\n", msg);
    }
}

static output_formatter_t* create_xml_formatter(void) {
    output_formatter_t* fmt = malloc(sizeof(output_formatter_t));
    if (!fmt) return NULL;

    xml_formatter_context_t* ctx = malloc(sizeof(xml_formatter_context_t));
    if (!ctx) {
        free(fmt);
        return NULL;
    }

    /* Default XML options */
    ctx->options.indent = 2;
    ctx->options.pretty_print = true;
    ctx->options.include_declaration = false;
    ctx->options.encoding = "UTF-8";

    fmt->type = OUTPUT_FORMAT_XML;
    fmt->context = ctx;
    fmt->print_document = xml_print_document_impl;
    fmt->print_element = xml_print_element_impl;
    fmt->print_nodeset = xml_print_nodeset_impl;
    fmt->print_string = xml_print_string_impl;
    fmt->print_number = xml_print_number_impl;
    fmt->print_boolean = xml_print_boolean_impl;
    fmt->print_error = xml_print_error_impl;
    fmt->print_success = xml_print_success_impl;

    return fmt;
}

/* ------------------------------------------------------------------------- */
/* JSON Formatter                                                            */
/* ------------------------------------------------------------------------- */

static void json_escape_string(const char* str, FILE* out) {
    if (!str) return;
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  fprintf(out, "\\\""); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '\b': fprintf(out, "\\b"); break;
            case '\f': fprintf(out, "\\f"); break;
            case '\n': fprintf(out, "\\n"); break;
            case '\r': fprintf(out, "\\r"); break;
            case '\t': fprintf(out, "\\t"); break;
            default:   fputc(*p, out); break;
        }
    }
}

static void json_print_element_recursive(
    TaurusElement elem,
    FILE* out,
    int level
) {
    if (!elem || !out) return;

    fprintf(out, "{");

    /* Element name - use API */
    const char* name = taurus_element_get_name(elem);
    fprintf(out, "\"name\":\"");
    json_escape_string(name ? name : "", out);
    fprintf(out, "\"");

    /* Attributes - handle both StringView and C string attributes */
    size_t attr_count = taurus_element_attribute_count(elem);
    if (attr_count > 0) {
        fprintf(out, ",\"attributes\":{");
        int first_attr = 1;
        /* Walk the attribute linked list */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
        while (attr) {
            /* Get attribute name (handle both StringView and C string) */
            const char* attr_name = attr->name;
            char* temp_name = NULL;
            if (!attr_name && !taurus_sv_is_empty(&attr->name_view)) {
                /* Convert StringView to C string for output */
                temp_name = taurus_sv_to_cstr(&attr->name_view);
                attr_name = temp_name;
            }

            /* Get attribute value (handle both StringView and C string) */
            const char* attr_value = attr->value;
            char* temp_value = NULL;
            if (!attr_value && !taurus_sv_is_empty(&attr->value_view)) {
                /* Convert StringView to C string for output */
                temp_value = taurus_sv_to_cstr(&attr->value_view);
                attr_value = temp_value;
            }

            if (attr_name) {
                if (!first_attr) fprintf(out, ",");
                first_attr = 0;

                fprintf(out, "\"");
                json_escape_string(attr_name, out);
                fprintf(out, "\":\"");
                if (attr_value) {
                    json_escape_string(attr_value, out);
                }
                fprintf(out, "\"");
            }

            /* Clean up temporary conversions */
            if (temp_name) free(temp_name);
            if (temp_value) free(temp_value);

            /* Move to next attribute */
            attr = attr->next;
        }
        fprintf(out, "}");
    }

    /* Text content - use API and free result */
    char* text_content = taurus_element_get_text_content(elem);
    if (text_content && text_content[0] != '\0') {
        fprintf(out, ",\"text\":\"");
        json_escape_string(text_content, out);
        fprintf(out, "\"");
    }
    TAURUS_FREE(text_content);

    /* Children - iterate using compact accessor functions */
    TaurusNode* first_child = taurus_elem_first_child(elem);
    if (first_child != NULL) {
        fprintf(out, ",\"children\":[");
        int first = 1;
        TaurusNode* child = first_child;
        while (child) {
            if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                if (!first) fprintf(out, ",");
                first = 0;
                json_print_element_recursive((TaurusElement)child, out, level + 1);
            }
            child = taurus_node_get_next_sibling(child);
        }
        fprintf(out, "]");
    }

    fprintf(out, "}");
}

static void json_print_document_impl(
    struct taurus_document* doc,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!doc || !out) return;

    if (doc->new_dom_root) {
        TaurusElement root = (TaurusElement)doc->new_dom_root;
        json_print_element_recursive(root, out, 0);
        fprintf(out, "\n");
    } else {
        fprintf(out, "{}\n");
    }
}

static void json_print_element_impl(
    TaurusElement elem,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!elem || !out) return;

    json_print_element_recursive(elem, out, 0);
    fprintf(out, "\n");
}

static void json_print_nodeset_impl(
    struct taurus_xpath_result* result,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!result || !out) return;
    if (result->type != XPATH_RESULT_NODESET) return;

    XPathNodeSet* nodeset = result->value.nodeset_value;
    if (!nodeset) {
        fprintf(out, "[]\n");
        return;
    }

    fprintf(out, "[");
    for (size_t i = 0; i < nodeset->count; i++) {
        if (i > 0) fprintf(out, ",");
        void* node_ptr = nodeset->nodes[i];

        /* Check node type - attribute nodes have different structure than elements */
        if (IS_ATTRIBUTE_NODE(node_ptr)) {
            /* Attribute node - print as {"name":"value"} format */
            TaurusAttributeNode* attr = (TaurusAttributeNode*)node_ptr;
            if (!attr) {
                fprintf(out, "null");
                continue;
            }

            fprintf(out, "{\"");
            if (attr->name) {
                json_escape_string(attr->name, out);
            }
            fprintf(out, "\":");
            if (attr->value) {
                fprintf(out, "\"");
                json_escape_string(attr->value, out);
                fprintf(out, "\"");
            } else {
                fprintf(out, "null");
            }
            fprintf(out, "}");
        } else if (IS_TEXT_NODE(node_ptr)) {
            /* Text node - print as JSON string */
            XPathTextNode* text = (XPathTextNode*)node_ptr;
            if (text && text->content) {
                fprintf(out, "\"");
                json_escape_string(text->content, out);
                fprintf(out, "\"");
            } else {
                fprintf(out, "null");
            }
        } else if (IS_ELEMENT_NODE(node_ptr)) {
            /* Element node - use existing recursive print function */
            TaurusElement elem = (TaurusElement)node_ptr;
            json_print_element_recursive(elem, out, 0);
        } else {
            /* Unknown node type - print as null */
            fprintf(out, "null");
        }
    }
    fprintf(out, "]\n");
}

static output_formatter_t* create_json_formatter(void) {
    output_formatter_t* fmt = malloc(sizeof(output_formatter_t));
    if (!fmt) return NULL;

    fmt->type = OUTPUT_FORMAT_JSON;
    fmt->context = NULL;
    fmt->print_document = json_print_document_impl;
    fmt->print_element = json_print_element_impl;
    fmt->print_nodeset = json_print_nodeset_impl;
    fmt->print_string = xml_print_string_impl;  /* Reuse */
    fmt->print_number = xml_print_number_impl;  /* Reuse */
    fmt->print_boolean = xml_print_boolean_impl; /* Reuse */
    fmt->print_error = xml_print_error_impl;    /* Reuse */
    fmt->print_success = xml_print_success_impl; /* Reuse */

    return fmt;
}

/* ------------------------------------------------------------------------- */
/* Text Formatter                                                            */
/* ------------------------------------------------------------------------- */

static void text_print_element_recursive(
    TaurusElement elem,
    FILE* out,
    int level
) {
    if (!elem || !out) return;

    /* Indent */
    for (int i = 0; i < level * 2; i++) {
        fputc(' ', out);
    }

    /* Element name - use API */
    const char* name = taurus_element_get_name(elem);
    fprintf(out, "%s", name ? name : "");

    /* Attributes - use compact accessor functions */
    size_t attr_count = taurus_element_attribute_count(elem);
    if (attr_count > 0) {
        fprintf(out, " {");
        int first_attr = 1;
        /* Walk the attribute linked list */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
        while (attr) {
            if (!first_attr) fprintf(out, ", ");
            first_attr = 0;
            if (attr->name) {
                fprintf(out, "%s=", attr->name);
                if (attr->value) {
                    fprintf(out, "\"%s\"", attr->value);
                } else {
                    fprintf(out, "\"\"");
                }
            }
            /* Move to next attribute */
            attr = attr->next;
        }
        fprintf(out, "}");
    }

    /* Text content - use API and free result */
    char* text_content = taurus_element_get_text_content(elem);
    if (text_content && text_content[0] != '\0') {
        fprintf(out, ": %s", text_content);
    }
    TAURUS_FREE(text_content);

    fprintf(out, "\n");

    /* Children - iterate through ALL children (including text nodes) */
    TaurusNode* child = taurus_elem_first_child(elem);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            text_print_element_recursive((TaurusElement)child, out, level + 1);
        }
        child = taurus_node_get_next_sibling(child);
    }
}

static void text_print_document_impl(
    struct taurus_document* doc,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!doc || !out) return;

    if (doc->new_dom_root) {
        TaurusElement root = (TaurusElement)doc->new_dom_root;
        text_print_element_recursive(root, out, 0);
    }
}

static void text_print_element_impl(
    TaurusElement elem,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!elem || !out) return;

    text_print_element_recursive(elem, out, 0);
}

static void text_print_nodeset_impl(
    struct taurus_xpath_result* result,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!result || !out) return;
    if (result->type != XPATH_RESULT_NODESET) return;

    XPathNodeSet* nodeset = result->value.nodeset_value;
    if (!nodeset) return;

    for (size_t i = 0; i < nodeset->count; i++) {
        void* node_ptr = nodeset->nodes[i];

        /* Check node type - attribute nodes have different structure than elements */
        if (IS_ATTRIBUTE_NODE(node_ptr)) {
            /* Attribute node - print as name="value" format */
            TaurusAttributeNode* attr = (TaurusAttributeNode*)node_ptr;
            if (attr && attr->name) {
                fprintf(out, "%s", attr->name);
                if (attr->value) {
                    fprintf(out, "=\"%s\"", attr->value);
                }
                fprintf(out, "\n");
            }
        } else if (IS_TEXT_NODE(node_ptr)) {
            /* Text node - print just the text content */
            XPathTextNode* text = (XPathTextNode*)node_ptr;
            if (text && text->content && text->content[0] != '\0') {
                fprintf(out, "%s\n", text->content);
            }
        } else if (IS_ELEMENT_NODE(node_ptr)) {
            /* Element node - use existing recursive print function */
            TaurusElement elem = (TaurusElement)node_ptr;
            text_print_element_recursive(elem, out, 0);
        }
    }
}

static output_formatter_t* create_text_formatter(void) {
    output_formatter_t* fmt = malloc(sizeof(output_formatter_t));
    if (!fmt) return NULL;

    fmt->type = OUTPUT_FORMAT_TEXT;
    fmt->context = NULL;
    fmt->print_document = text_print_document_impl;
    fmt->print_element = text_print_element_impl;
    fmt->print_nodeset = text_print_nodeset_impl;
    fmt->print_string = xml_print_string_impl;  /* Reuse */
    fmt->print_number = xml_print_number_impl;  /* Reuse */
    fmt->print_boolean = xml_print_boolean_impl; /* Reuse */
    fmt->print_error = xml_print_error_impl;    /* Reuse */
    fmt->print_success = xml_print_success_impl; /* Reuse */

    return fmt;
}

/* ------------------------------------------------------------------------- */
/* Formatter Factory                                                         */
/* ------------------------------------------------------------------------- */

output_formatter_t* output_formatter_create(output_format_t type) {
    switch (type) {
        case OUTPUT_FORMAT_XML:
            return create_xml_formatter();
        case OUTPUT_FORMAT_JSON:
            return create_json_formatter();
        case OUTPUT_FORMAT_TEXT:
            return create_text_formatter();
        default:
            return NULL;
    }
}

void output_formatter_free(output_formatter_t* fmt) {
    if (!fmt) return;

    if (fmt->context) {
        free(fmt->context);
    }
    free(fmt);
}

/* ------------------------------------------------------------------------- */
/* Format-Specific Options                                                  */
/* ------------------------------------------------------------------------- */

void output_formatter_set_xml_options(
    output_formatter_t* fmt,
    const xml_format_options_t* options
) {
    if (!fmt || fmt->type != OUTPUT_FORMAT_XML || !options) return;

    xml_formatter_context_t* ctx = (xml_formatter_context_t*)fmt->context;
    if (ctx) {
        ctx->options = *options;
    }
}

void output_formatter_set_json_options(
    output_formatter_t* fmt,
    const json_format_options_t* options
) {
    /* Stub for now */
    (void)fmt;
    (void)options;
}

void output_formatter_set_text_options(
    output_formatter_t* fmt,
    const text_format_options_t* options
) {
    /* Stub for now */
    (void)fmt;
    (void)options;
}

/* ------------------------------------------------------------------------- */
/* Convenience Functions                                                     */
/* ------------------------------------------------------------------------- */

void output_print_xpath_result(
    struct taurus_xpath_result* result,
    FILE* out,
    output_formatter_t* fmt
) {
    (void)result;
    (void)fmt;
    if (!out) return;

    /* Stub - will implement with public API in Session 94 */
    fprintf(out, "XPath result output not yet implemented\n");
}

void output_print_count(
    size_t count,
    FILE* out,
    output_formatter_t* fmt
) {
    if (!out) return;

    if (fmt && fmt->type == OUTPUT_FORMAT_JSON) {
        fprintf(out, "{\"count\": %zu}\n", count);
    } else {
        fprintf(out, "%zu\n", count);
    }
}

/* ------------------------------------------------------------------------- */
/* Color Output Support                                                      */
/* ------------------------------------------------------------------------- */

bool output_supports_color(FILE* out) {
    if (!out) return false;

    /* Check if output is a TTY */
    int fd = fileno(out);
    if (fd < 0) return false;

    return isatty(fd) != 0;
}

void output_print_colored(
    const char* text,
    color_code_t color,
    FILE* out
) {
    if (!text || !out) return;

    if (!output_supports_color(out)) {
        fprintf(out, "%s", text);
        return;
    }

    const char* color_code;
    switch (color) {
        case COLOR_RED:     color_code = "\033[31m"; break;
        case COLOR_GREEN:   color_code = "\033[32m"; break;
        case COLOR_YELLOW:  color_code = "\033[33m"; break;
        case COLOR_BLUE:    color_code = "\033[34m"; break;
        case COLOR_CYAN:    color_code = "\033[36m"; break;
        case COLOR_MAGENTA: color_code = "\033[35m"; break;
        default:            color_code = ""; break;
    }

    fprintf(out, "%s%s\033[0m", color_code, text);
}