/**
 * @file output.c
 * @brief Output formatting implementation for Leptris CLI
 */

#include "output.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <io.h>
#  define isatty(fd) _isatty(fd)
#else
#  include <unistd.h>
#endif

#include "leptris.h"  /* Public API only — the CLI layer contract */

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
    LeptrisElement elem,
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
    const char* name = leptris_element_name(elem);
    fprintf(out, "<%s", name ? name : "");

    /* Attributes - public handle iteration (O(n), not O(n^2) index walks) */
    for (LeptrisAttribute attr = leptris_element_first_attribute(elem);
         attr; attr = leptris_attribute_next(attr)) {
        const char* attr_name = leptris_attribute_get_name(attr);
        const char* attr_value = leptris_attribute_get_value(elem, attr);

        if (attr_name && attr_name[0]) {
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
    }

    /* Namespace declarations - output xmlns attributes if element has namespace */
    const char* ns_uri = leptris_element_namespace(elem);
    const char* ns_prefix = leptris_element_prefix(elem);
    if (ns_uri) {
        fprintf(out, " xmlns");
        if (ns_prefix) {
            fprintf(out, ":%s", ns_prefix);
        }
        fprintf(out, "=\"%s\"", ns_uri);
    }

    /* Namespace declarations stored on this element (xmlns:prefix="uri") */
    size_t ns_count = leptris_element_namespace_count(elem);
    for (size_t i = 0; i < ns_count; i++) {
        const char* decl_prefix = leptris_element_namespace_decl_prefix(elem, i);
        const char* decl_uri = leptris_element_namespace_decl_uri(elem, i);
        fprintf(out, " xmlns");
        if (decl_prefix) {
            fprintf(out, ":%s", decl_prefix);
        }
        fprintf(out, "=\"%s\"", decl_uri ? decl_uri : "");
    }

    /* Check if element has children (including text nodes) */
    LeptrisNodeRef first_child = leptris_node_first_child(leptris_element_as_node(elem));
    int has_children = (first_child != NULL);
    int first_is_element = first_child &&
        leptris_node_get_type(first_child) == LEPTRIS_NODE_TYPE_ELEMENT;

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
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(elem));
    while (child) {
        int child_type = leptris_node_get_type(child);
        if (child_type == LEPTRIS_NODE_TYPE_ELEMENT) {
            /* Element child - recurse with indentation */
            xml_print_element_recursive((LeptrisElement)child, out, level + 1, ctx);
        }
        else if (child_type == LEPTRIS_NODE_TYPE_TEXT) {
            /* Text node - escape and print. The public getter
             * materializes borrowed (non-NUL-terminated) content. */
            const char* content = leptris_text_node_get_content(child);
            if (content) {
                /* In compact mode, skip whitespace-only text nodes */
                if (!ctx->options.pretty_print) {
                    /* Check if content is whitespace-only */
                    const char* p = content;
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                        p++;
                    }
                    if (*p == '\0') {
                        /* Whitespace-only, skip in compact mode */
                        child = leptris_node_next_sibling(child);
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
            child = leptris_node_next_sibling(child);
            continue;
        }
        else if (child_type == LEPTRIS_NODE_TYPE_CDATA) {
            /* CDATA node - print as <![CDATA[...]]> */
            const char* content = leptris_text_node_get_content(child);
            if (content) {
                fprintf(out, "<![CDATA[%s]]>", content);
            }
            child = leptris_node_next_sibling(child);
            continue;
        }
        else if (child_type == LEPTRIS_NODE_TYPE_COMMENT) {
            /* Comment node - print as <!--...--> */
            const char* content = leptris_comment_node_get_content(child);
            if (content) {
                fprintf(out, "<!--%s-->", content);
            }
            child = leptris_node_next_sibling(child);
            continue;
        }
        else if (child_type == LEPTRIS_NODE_TYPE_PI) {
            /* Processing Instruction - print as <?target data?> */
            const char* target = leptris_pi_node_get_target(child);
            if (target) {
                fprintf(out, "<?%s", target);
                const char* data = leptris_pi_node_get_data(child);
                if (data) {
                    fprintf(out, " %s", data);
                }
                fprintf(out, "?>");
            }
            child = leptris_node_next_sibling(child);
            continue;
        }
        /* DOCTYPE nodes are not printed as children of elements */

        /* For element nodes, get next sibling using node API */
        child = leptris_node_next_sibling(child);
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
    LeptrisDocument doc,
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
    LeptrisElement root = leptris_document_root(doc);
    if (root) {
        xml_print_element_recursive(root, out, 0, xml_ctx);
    }
}

static void xml_print_element_impl(
    LeptrisElement elem,
    FILE* out,
    void* ctx
) {
    if (!elem || !out) return;

    xml_formatter_context_t* xml_ctx = (xml_formatter_context_t*)ctx;
    xml_print_element_recursive(elem, out, 0, xml_ctx);
}

static void xml_print_nodeset_impl(
    LeptrisXPathResult result,
    FILE* out,
    void* ctx
) {
    if (!result || !out) return;
    if (leptris_xpath_result_type(result) != LEPTRIS_XPATH_NODESET) return;

    xml_formatter_context_t* xml_ctx = (xml_formatter_context_t*)ctx;

    /* Print each node in the nodeset based on type */
    size_t count = leptris_xpath_result_count(result);
    for (size_t i = 0; i < count; i++) {

        /* Mixed nodeset - dispatch on the public kind */
        LeptrisXPathNodeKind kind = leptris_xpath_result_node_kind(result, i);
        if (kind == LEPTRIS_XPATH_NODE_ATTRIBUTE) {
            const char* name = leptris_xpath_result_node_name(result, i);
            const char* value = leptris_xpath_result_node_value(result, i);
            if (name && name[0] != '\0') {
                fprintf(out, "%s", name);
                if (value) {
                    fprintf(out, "=\"%s\"", value);
                }
                fprintf(out, "\n");
            }
        } else if (kind == LEPTRIS_XPATH_NODE_TEXT) {
            const char* content = leptris_xpath_result_node_value(result, i);
            if (content && content[0] != '\0') {
                fprintf(out, "%s\n", content);
            }
        } else if (kind == LEPTRIS_XPATH_NODE_ELEMENT) {
            xml_print_element_recursive(
                leptris_node_as_element(leptris_xpath_result_get_node(result, i)),
                out, 0, xml_ctx);
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
    LeptrisElement elem,
    FILE* out,
    int level
) {
    if (!elem || !out) return;

    fprintf(out, "{");

    /* Element name - use API */
    const char* name = leptris_element_name(elem);
    fprintf(out, "\"name\":\"");
    json_escape_string(name ? name : "", out);
    fprintf(out, "\"");

    /* Attributes - public handle iteration */
    if (leptris_element_attribute_count(elem) > 0) {
        fprintf(out, ",\"attributes\":{");
        int first_attr = 1;
        for (LeptrisAttribute attr = leptris_element_first_attribute(elem);
             attr; attr = leptris_attribute_next(attr)) {
            const char* attr_name = leptris_attribute_get_name(attr);
            const char* attr_value = leptris_attribute_get_value(elem, attr);

            if (attr_name && attr_name[0]) {
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

        }
        fprintf(out, "}");
    }

    /* Text content - document-owned string, no free */
    const char* text_content = leptris_element_text(elem);
    if (text_content && text_content[0] != '\0') {
        fprintf(out, ",\"text\":\"");
        json_escape_string(text_content, out);
        fprintf(out, "\"");
    }

    /* Children - element children via the public node API */
    LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(elem));
    if (child != NULL) {
        fprintf(out, ",\"children\":[");
        int first = 1;
        for (; child; child = leptris_node_next_sibling(child)) {
            if (leptris_node_get_type(child) == LEPTRIS_NODE_TYPE_ELEMENT) {
                if (!first) fprintf(out, ",");
                first = 0;
                json_print_element_recursive((LeptrisElement)child, out, level + 1);
            }
        }
        fprintf(out, "]");
    }

    fprintf(out, "}");
}

static void json_print_document_impl(
    LeptrisDocument doc,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!doc || !out) return;

    LeptrisElement root = leptris_document_root(doc);
    if (root) {
        json_print_element_recursive(root, out, 0);
        fprintf(out, "\n");
    } else {
        fprintf(out, "{}\n");
    }
}

static void json_print_element_impl(
    LeptrisElement elem,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!elem || !out) return;

    json_print_element_recursive(elem, out, 0);
    fprintf(out, "\n");
}

static void json_print_nodeset_impl(
    LeptrisXPathResult result,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!result || !out) return;
    if (leptris_xpath_result_type(result) != LEPTRIS_XPATH_NODESET) return;

    size_t count = leptris_xpath_result_count(result);
    fprintf(out, "[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) fprintf(out, ",");
        LeptrisXPathNodeKind kind = leptris_xpath_result_node_kind(result, i);
        if (kind == LEPTRIS_XPATH_NODE_ATTRIBUTE) {
            fprintf(out, "{\"");
            const char* name = leptris_xpath_result_node_name(result, i);
            if (name) {
                json_escape_string(name, out);
            }
            fprintf(out, "\":");
            const char* value = leptris_xpath_result_node_value(result, i);
            if (value) {
                fprintf(out, "\"");
                json_escape_string(value, out);
                fprintf(out, "\"");
            } else {
                fprintf(out, "null");
            }
            fprintf(out, "}");
        } else if (kind == LEPTRIS_XPATH_NODE_TEXT) {
            const char* content = leptris_xpath_result_node_value(result, i);
            if (content) {
                fprintf(out, "\"");
                json_escape_string(content, out);
                fprintf(out, "\"");
            } else {
                fprintf(out, "null");
            }
        } else if (kind == LEPTRIS_XPATH_NODE_ELEMENT) {
            json_print_element_recursive(
                leptris_node_as_element(leptris_xpath_result_get_node(result, i)),
                out, 0);
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
    LeptrisElement elem,
    FILE* out,
    int level
) {
    if (!elem || !out) return;

    /* Indent */
    for (int i = 0; i < level * 2; i++) {
        fputc(' ', out);
    }

    /* Element name - use API */
    const char* name = leptris_element_name(elem);
    fprintf(out, "%s", name ? name : "");

    /* Attributes - public handle iteration */
    if (leptris_element_attribute_count(elem) > 0) {
        fprintf(out, " {");
        int first_attr = 1;
        for (LeptrisAttribute attr = leptris_element_first_attribute(elem);
             attr; attr = leptris_attribute_next(attr)) {
            if (!first_attr) fprintf(out, ", ");
            first_attr = 0;
            fprintf(out, "%s=", leptris_attribute_get_name(attr));
            const char* av = leptris_attribute_get_value(elem, attr);
            fprintf(out, "\"%s\"", av ? av : "");
        }
        fprintf(out, "}");
    }

    /* Text content - document-owned string, no free */
    const char* text_content = leptris_element_text(elem);
    if (text_content && text_content[0] != '\0') {
        fprintf(out, ": %s", text_content);
    }

    fprintf(out, "\n");

    /* Children - element children via the public node API */
    for (LeptrisNodeRef child = leptris_node_first_child(leptris_element_as_node(elem));
         child; child = leptris_node_next_sibling(child)) {
        if (leptris_node_get_type(child) == LEPTRIS_NODE_TYPE_ELEMENT) {
            text_print_element_recursive((LeptrisElement)child, out, level + 1);
        }
    }
}

static void text_print_document_impl(
    LeptrisDocument doc,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!doc || !out) return;

    LeptrisElement root = leptris_document_root(doc);
    if (root) {
        text_print_element_recursive(root, out, 0);
    }
}

static void text_print_element_impl(
    LeptrisElement elem,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!elem || !out) return;

    text_print_element_recursive(elem, out, 0);
}

static void text_print_nodeset_impl(
    LeptrisXPathResult result,
    FILE* out,
    void* ctx
) {
    (void)ctx;
    if (!result || !out) return;
    if (leptris_xpath_result_type(result) != LEPTRIS_XPATH_NODESET) return;

    size_t count = leptris_xpath_result_count(result);
    for (size_t i = 0; i < count; i++) {
        LeptrisXPathNodeKind kind = leptris_xpath_result_node_kind(result, i);
        if (kind == LEPTRIS_XPATH_NODE_ATTRIBUTE) {
            const char* name = leptris_xpath_result_node_name(result, i);
            if (name) {
                fprintf(out, "%s", name);
                const char* value = leptris_xpath_result_node_value(result, i);
                if (value) {
                    fprintf(out, "=\"%s\"", value);
                }
                fprintf(out, "\n");
            }
        } else if (kind == LEPTRIS_XPATH_NODE_TEXT) {
            const char* content = leptris_xpath_result_node_value(result, i);
            if (content && content[0] != '\0') {
                fprintf(out, "%s\n", content);
            }
        } else if (kind == LEPTRIS_XPATH_NODE_ELEMENT) {
            text_print_element_recursive(
                leptris_node_as_element(leptris_xpath_result_get_node(result, i)),
                out, 0);
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
    struct leptris_xpath_result* result,
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