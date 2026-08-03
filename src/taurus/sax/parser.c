/**
 * @file sax/parser.c
 * @brief SAX parser implementation
 *
 * Event-driven XML parsing without DOM tree construction.
 */

#include "../../include/taurus/sax/sax.h"
#include "../taurus_internal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/**
 * SAX parser state
 */
struct TaurusSAXParser {
    TaurusSAXHandler* handler;
    void* user_data;

    /* Parser state */
    const char* pos;
    const char* end;
    int line;
    int column;
    int has_error;
    char error_message[256];
};

/* ============================================================================
 * Parser Utilities
 * ============================================================================ */

static int sax_at_end(TaurusSAXParser* p) {
    return p->pos >= p->end;
}

static char sax_peek(TaurusSAXParser* p) {
    if (sax_at_end(p)) return '\0';
    return *p->pos;
}

static char sax_advance(TaurusSAXParser* p) {
    if (sax_at_end(p)) return '\0';

    char c = *p->pos;
    p->pos++;

    if (c == '\n') {
        p->line++;
        p->column = 1;
    } else {
        p->column++;
    }

    return c;
}

static int sax_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void sax_skip_whitespace(TaurusSAXParser* p) {
    while (!sax_at_end(p) && sax_is_whitespace(sax_peek(p))) {
        sax_advance(p);
    }
}

static int sax_match(TaurusSAXParser* p, const char* str) {
    size_t len = strlen(str);
    if (p->pos + len > p->end) return 0;
    return strncmp(p->pos, str, len) == 0;
}

static void sax_set_error(TaurusSAXParser* p, const char* message) {
    snprintf(p->error_message, sizeof(p->error_message), "%s", message);
    p->has_error = 1;

    if (p->handler && p->handler->error) {
        p->handler->error(p->user_data, message, p->line, p->column);
    }
}

static int sax_is_name_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

static int sax_is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.';
}

/* ============================================================================
 * SAX Event Emitters
 * ============================================================================ */

static void emit_start_document(TaurusSAXParser* p) {
    if (p->handler && p->handler->start_document) {
        p->handler->start_document(p->user_data);
    }
}

static void emit_end_document(TaurusSAXParser* p) {
    if (p->handler && p->handler->end_document) {
        p->handler->end_document(p->user_data);
    }
}

static void emit_characters(TaurusSAXParser* p, const char* text, size_t len) {
    if (p->handler && p->handler->characters && len > 0) {
        p->handler->characters(p->user_data, text, len);
    }
}

/* ============================================================================
 * SAX Parsing Functions
 * ============================================================================ */

/**
 * Parse element name
 */
static char* sax_parse_name(TaurusSAXParser* p) {
    const char* start = p->pos;

    if (!sax_is_name_start(sax_peek(p))) {
        sax_set_error(p, "Expected element name");
        return NULL;
    }

    while (!sax_at_end(p) && sax_is_name_char(sax_peek(p))) {
        sax_advance(p);
    }

    size_t len = p->pos - start;
    char* name = (char*)malloc(len + 1);
    if (!name) return NULL;

    memcpy(name, start, len);
    name[len] = '\0';
    return name;
}

/**
 * Parse attribute value
 */
static char* sax_parse_attr_value(TaurusSAXParser* p) {
    char quote = sax_peek(p);
    if (quote != '"' && quote != '\'') {
        sax_set_error(p, "Expected quote for attribute value");
        return NULL;
    }

    sax_advance(p); /* Skip opening quote */
    const char* start = p->pos;

    /* Find closing quote */
    while (!sax_at_end(p) && sax_peek(p) != quote) {
        sax_advance(p);
    }

    size_t len = p->pos - start;
    char* value = (char*)malloc(len + 1);
    if (!value) return NULL;

    memcpy(value, start, len);
    value[len] = '\0';

    if (sax_peek(p) == quote) {
        sax_advance(p); /* Skip closing quote */
    }

    return value;
}

/**
 * Parse element and emit SAX events
 */
static int sax_parse_element(TaurusSAXParser* p) {
    /* Expect '<' */
    if (sax_peek(p) != '<') {
        sax_set_error(p, "Expected '<'");
        return -1;
    }
    sax_advance(p);

    /* Parse element name */
    char* name = sax_parse_name(p);
    if (!name) return -1;

    /* Parse attributes */
    const char** attrs = NULL;
    size_t attr_count = 0;
    size_t attr_capacity = 0;

    while (!sax_at_end(p)) {
        sax_skip_whitespace(p);

        char c = sax_peek(p);

        /* Check for end of opening tag */
        if (c == '>') {
            sax_advance(p);
            break;
        }

        /* Check for self-closing tag */
        if (c == '/' && p->pos + 1 < p->end && p->pos[1] == '>') {
            sax_advance(p); /* Skip '/' */
            sax_advance(p); /* Skip '>' */

            /* Emit start_prefix_mapping for each xmlns* attribute.
             * No allocations here — we re-iterate attrs at end-of-element
             * to fire end_prefix_mapping, so there's nothing to free. */
            if (attrs && p->handler && p->handler->start_prefix_mapping) {
                for (size_t i = 0; i < attr_count * 2; i += 2) {
                    const char* attr_name = attrs[i];
                    const char* attr_value = attrs[i + 1];

                    if (strcmp(attr_name, "xmlns") == 0) {
                        p->handler->start_prefix_mapping(p->user_data, "", attr_value);
                    } else if (strncmp(attr_name, "xmlns:", 6) == 0) {
                        p->handler->start_prefix_mapping(p->user_data, attr_name + 6, attr_value);
                    }
                }
            }

            /* Emit start and end events for self-closing tag */
            if (p->handler && p->handler->start_element) {
                p->handler->start_element(p->user_data, name, attrs ? attrs : (const char*[]){NULL});
            }
            if (p->handler && p->handler->end_element) {
                p->handler->end_element(p->user_data, name);
            }

            /* Emit end_prefix_mapping by re-iterating attrs */
            if (attrs && p->handler && p->handler->end_prefix_mapping) {
                for (size_t i = 0; i < attr_count * 2; i += 2) {
                    const char* attr_name = attrs[i];

                    if (strcmp(attr_name, "xmlns") == 0) {
                        p->handler->end_prefix_mapping(p->user_data, "");
                    } else if (strncmp(attr_name, "xmlns:", 6) == 0) {
                        p->handler->end_prefix_mapping(p->user_data, attr_name + 6);
                    }
                }
            }

            free(name);
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                    free((void*)attrs[i]);
                }
                free(attrs);
            }
            return 0;
        }

        /* Parse attribute name */
        char* attr_name = sax_parse_name(p);
        if (!attr_name) {
            free(name);
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                    free((void*)attrs[i]);
                }
                free(attrs);
            }
            return -1;
        }

        sax_skip_whitespace(p);

        /* Expect '=' */
        if (sax_peek(p) != '=') {
            sax_set_error(p, "Expected '=' after attribute name");
            free(attr_name);
            free(name);
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                    free((void*)attrs[i]);
                }
                free(attrs);
            }
            return -1;
        }
        sax_advance(p);

        sax_skip_whitespace(p);

        /* Parse attribute value */
        char* attr_value = sax_parse_attr_value(p);
        if (!attr_value) {
            free(attr_name);
            free(name);
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                    free((void*)attrs[i]);
                }
                free(attrs);
            }
            return -1;
        }

        /* Grow attribute array */
        if (attr_count * 2 + 2 >= attr_capacity) {
            attr_capacity = attr_capacity == 0 ? 8 : attr_capacity * 2;
            const char** new_attrs = (const char**)realloc(attrs, (attr_capacity + 1) * sizeof(char*));
            if (!new_attrs) {
                free(attr_name);
                free(attr_value);
                free(name);
                if (attrs) {
                    for (size_t i = 0; i < attr_count * 2; i++) {
                        free((void*)attrs[i]);
                    }
                    free(attrs);
                }
                return -1;
            }
            attrs = new_attrs;
        }

        attrs[attr_count * 2] = attr_name;
        attrs[attr_count * 2 + 1] = attr_value;
        attr_count++;
        attrs[attr_count * 2] = NULL; /* NULL-terminate array */
    }

    /* Emit start_prefix_mapping for each xmlns* attribute.
     * No allocations — we re-iterate attrs at end-of-element. */
    if (attrs && p->handler && p->handler->start_prefix_mapping) {
        for (size_t i = 0; i < attr_count * 2; i += 2) {
            const char* attr_name = attrs[i];
            const char* attr_value = attrs[i + 1];

            if (strcmp(attr_name, "xmlns") == 0) {
                p->handler->start_prefix_mapping(p->user_data, "", attr_value);
            } else if (strncmp(attr_name, "xmlns:", 6) == 0) {
                p->handler->start_prefix_mapping(p->user_data, attr_name + 6, attr_value);
            }
        }
    }

    /* Emit start_element event */
    if (p->handler && p->handler->start_element) {
        p->handler->start_element(p->user_data, name, attrs ? attrs : (const char*[]){NULL});
    }

    /* Parse children */
    while (!sax_at_end(p)) {
        sax_skip_whitespace(p);

        /* Check for closing tag */
        if (sax_match(p, "</")) {
            /* Skip "</" */
            sax_advance(p);
            sax_advance(p);

            /* Parse closing tag name */
            char* close_name = sax_parse_name(p);
            if (!close_name || strcmp(close_name, name) != 0) {
                sax_set_error(p, "Mismatched closing tag");
                if (close_name) free(close_name);
                free(name);
                if (attrs) {
                    for (size_t i = 0; i < attr_count * 2; i++) {
                        free((void*)attrs[i]);
                    }
                    free(attrs);
                }
                return -1;
            }
            free(close_name);

            sax_skip_whitespace(p);

            /* Expect '>' */
            if (sax_peek(p) == '>') {
                sax_advance(p);
            }

            break; /* End of element */
        }

        /* Check for child element */
        if (sax_peek(p) == '<') {
            /* Could be element, comment, CDATA, PI */
            if (sax_match(p, "<!--")) {
                /* Comment - extract and emit callback */
                /* Skip "<!--" */
                for (int i = 0; i < 4; i++) sax_advance(p);
                const char* start = p->pos;

                /* Find "-->" */
                while (!sax_at_end(p) && !sax_match(p, "-->")) {
                    sax_advance(p);
                }

                size_t len = p->pos - start;

                /* Emit comment event */
                if (p->handler && p->handler->comment && len > 0) {
                    char* comment = (char*)malloc(len + 1);
                    if (comment) {
                        memcpy(comment, start, len);
                        comment[len] = '\0';
                        p->handler->comment(p->user_data, comment);
                        free(comment);
                    }
                }

                /* Skip "-->" */
                if (sax_match(p, "-->")) {
                    for (int i = 0; i < 3; i++) sax_advance(p);
                }
            } else if (sax_match(p, "<![CDATA[")) {
                /* CDATA - extract and emit callback */
                /* Skip "<![CDATA[" */
                for (int i = 0; i < 9; i++) sax_advance(p);
                const char* start = p->pos;

                /* Find "]]>" */
                while (!sax_at_end(p) && !sax_match(p, "]]>")) {
                    sax_advance(p);
                }

                size_t len = p->pos - start;

                /* Emit CDATA event */
                if (p->handler && p->handler->cdata && len > 0) {
                    char* cdata = (char*)malloc(len + 1);
                    if (cdata) {
                        memcpy(cdata, start, len);
                        cdata[len] = '\0';
                        p->handler->cdata(p->user_data, cdata);
                        free(cdata);
                    }
                }

                /* Skip "]]>" */
                if (sax_match(p, "]]>")) {
                    for (int i = 0; i < 3; i++) sax_advance(p);
                }
            } else if (sax_match(p, "<?")) {
                /* Processing Instruction - extract target and data */
                /* Skip "<?" */
                sax_advance(p);
                sax_advance(p);

                /* Parse PI target */
                const char* target_start = p->pos;
                while (!sax_at_end(p) && !sax_is_whitespace(sax_peek(p)) && !sax_match(p, "?>")) {
                    sax_advance(p);
                }
                size_t target_len = p->pos - target_start;

                char* target = NULL;
                if (target_len > 0) {
                    target = (char*)malloc(target_len + 1);
                    if (target) {
                        memcpy(target, target_start, target_len);
                        target[target_len] = '\0';
                    }
                }

                /* Skip whitespace before PI data */
                sax_skip_whitespace(p);

                /* Parse PI data */
                const char* data_start = p->pos;
                while (!sax_at_end(p) && !sax_match(p, "?>")) {
                    sax_advance(p);
                }
                size_t data_len = p->pos - data_start;

                char* data = NULL;
                if (data_len > 0) {
                    data = (char*)malloc(data_len + 1);
                    if (data) {
                        memcpy(data, data_start, data_len);
                        data[data_len] = '\0';
                    }
                }

                /* Emit processing instruction event */
                if (p->handler && p->handler->processing_instruction && target) {
                    p->handler->processing_instruction(p->user_data, target, data);
                }

                /* Free allocated strings */
                if (target) free(target);
                if (data) free(data);

                /* Skip "?>" */
                if (sax_match(p, "?>")) {
                    sax_advance(p);
                    sax_advance(p);
                }
            } else {
                /* Child element */
                if (sax_parse_element(p) < 0) {
                    free(name);
                    if (attrs) {
                        for (size_t i = 0; i < attr_count * 2; i++) {
                            free((void*)attrs[i]);
                        }
                        free(attrs);
                    }
                    return -1;
                }
            }
        } else {
            /* Text content */
            const char* start = p->pos;
            while (!sax_at_end(p) && sax_peek(p) != '<') {
                sax_advance(p);
            }
            size_t len = p->pos - start;
            if (len > 0) {
                emit_characters(p, start, len);
            }
        }
    }

    /* Emit end_element event */
    if (p->handler && p->handler->end_element) {
        p->handler->end_element(p->user_data, name);
    }

    /* Emit end_prefix_mapping by re-iterating attrs.
     * Order is the reverse of start_prefix_mapping, but the SAX spec
     * does not require a specific order for end_prefix_mapping. */
    if (attrs && p->handler && p->handler->end_prefix_mapping) {
        for (size_t i = 0; i < attr_count * 2; i += 2) {
            const char* attr_name = attrs[i];

            if (strcmp(attr_name, "xmlns") == 0) {
                p->handler->end_prefix_mapping(p->user_data, "");
            } else if (strncmp(attr_name, "xmlns:", 6) == 0) {
                p->handler->end_prefix_mapping(p->user_data, attr_name + 6);
            }
        }
    }

    free(name);
    if (attrs) {
        for (size_t i = 0; i < attr_count * 2; i++) {
            free((void*)attrs[i]);
        }
        free(attrs);
    }

    return 0;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Parse XML using SAX (one-shot API)
 */
int taurus_sax_parse(const char* xml, size_t len,
                     TaurusSAXHandler* handler,
                     void* user_data) {
    if (!xml || len == 0 || !handler) return -1;

    TaurusSAXParser parser = {0};
    parser.handler = handler;
    parser.user_data = user_data;
    parser.pos = xml;
    parser.end = xml + len;
    parser.line = 1;
    parser.column = 1;
    parser.has_error = 0;

    /* Emit start_document */
    emit_start_document(&parser);

    /* Skip whitespace and XML declaration */
    sax_skip_whitespace(&parser);

    if (sax_match(&parser, "<?xml")) {
        /* Check if this is actually XML declaration (not <?xml-stylesheet etc) */
        const char* check_pos = parser.pos + 5; /* After "<?xml" */
        if (check_pos < parser.end && (sax_is_whitespace(*check_pos) || *check_pos == '?')) {
            /* Skip XML declaration */
            while (!sax_at_end(&parser) && !sax_match(&parser, "?>")) {
                sax_advance(&parser);
            }
            if (sax_match(&parser, "?>")) {
                sax_advance(&parser);
                sax_advance(&parser);
            }
            sax_skip_whitespace(&parser);
        }
    }

    /* Skip DOCTYPE */
    if (sax_match(&parser, "<!DOCTYPE")) {
        while (!sax_at_end(&parser) && sax_peek(&parser) != '>') {
            sax_advance(&parser);
        }
        if (sax_peek(&parser) == '>') {
            sax_advance(&parser);
        }
        sax_skip_whitespace(&parser);
    }

    /* Handle pre-root content (comments, PIs) */
    while (!sax_at_end(&parser)) {
        sax_skip_whitespace(&parser);

        if (sax_match(&parser, "<!--")) {
            /* Comment - extract and emit callback */
            /* Skip "<!--" */
            for (int i = 0; i < 4; i++) sax_advance(&parser);
            const char* start = parser.pos;

            /* Find "-->" */
            while (!sax_at_end(&parser) && !sax_match(&parser, "-->")) {
                sax_advance(&parser);
            }

            size_t len = parser.pos - start;

            /* Emit comment event */
            if (parser.handler && parser.handler->comment && len > 0) {
                char* comment = (char*)malloc(len + 1);
                if (comment) {
                    memcpy(comment, start, len);
                    comment[len] = '\0';
                    parser.handler->comment(parser.user_data, comment);
                    free(comment);
                }
            }

            /* Skip "-->" */
            if (sax_match(&parser, "-->")) {
                for (int i = 0; i < 3; i++) sax_advance(&parser);
            }
        } else if (sax_match(&parser, "<?")) {
            /* Processing Instruction - extract target and data */
            /* Skip "<?" */
            sax_advance(&parser);
            sax_advance(&parser);

            /* Parse PI target */
            const char* target_start = parser.pos;
            while (!sax_at_end(&parser) && !sax_is_whitespace(sax_peek(&parser)) && !sax_match(&parser, "?>")) {
                sax_advance(&parser);
            }
            size_t target_len = parser.pos - target_start;

            char* target = NULL;
            if (target_len > 0) {
                target = (char*)malloc(target_len + 1);
                if (target) {
                    memcpy(target, target_start, target_len);
                    target[target_len] = '\0';
                }
            }

            /* Skip whitespace before PI data */
            sax_skip_whitespace(&parser);

            /* Parse PI data */
            const char* data_start = parser.pos;
            while (!sax_at_end(&parser) && !sax_match(&parser, "?>")) {
                sax_advance(&parser);
            }
            size_t data_len = parser.pos - data_start;

            char* data = NULL;
            if (data_len > 0) {
                data = (char*)malloc(data_len + 1);
                if (data) {
                    memcpy(data, data_start, data_len);
                    data[data_len] = '\0';
                }
            }

            /* Emit processing instruction event */
            if (parser.handler && parser.handler->processing_instruction && target) {
                parser.handler->processing_instruction(parser.user_data, target, data);
            }

            /* Free allocated strings */
            if (target) free(target);
            if (data) free(data);

            /* Skip "?>" */
            if (sax_match(&parser, "?>")) {
                sax_advance(&parser);
                sax_advance(&parser);
            }
        } else if (sax_peek(&parser) == '<' && !sax_match(&parser, "<!")) {
            /* Found root element */
            break;
        } else if (!sax_at_end(&parser) && !sax_is_whitespace(sax_peek(&parser))) {
            /* Unexpected content - skip it */
            sax_advance(&parser);
        } else {
            /* Just whitespace, continue */
            break;
        }
    }

    /* Parse root element */
    if (sax_parse_element(&parser) < 0) {
        return -1;
    }

    /* Emit end_document */
    emit_end_document(&parser);

    return parser.has_error ? -1 : 0;
}

/**
 * Create SAX parser for incremental parsing
 */
TaurusSAXParser* taurus_sax_parser_create(TaurusSAXHandler* handler, void* user_data) {
    if (!handler) return NULL;

    TaurusSAXParser* parser = (TaurusSAXParser*)malloc(sizeof(TaurusSAXParser));
    if (!parser) return NULL;

    parser->handler = handler;
    parser->user_data = user_data;
    parser->pos = NULL;
    parser->end = NULL;
    parser->line = 1;
    parser->column = 1;
    parser->has_error = 0;
    parser->error_message[0] = '\0';

    return parser;
}

/**
 * Feed XML chunk (incremental parsing - basic implementation)
 */
int taurus_sax_parser_feed(TaurusSAXParser* parser,
                            const char* xml,
                            size_t len,
                            int is_final) {
    if (!parser || !xml) return -1;

    /* For now, simple implementation that requires complete XML in one chunk */
    /* TODO: Implement true incremental parsing in Session 2 */
    if (is_final) {
        return taurus_sax_parse(xml, len, parser->handler, parser->user_data);
    }

    return 0;
}

/**
 * Free SAX parser
 */
void taurus_sax_parser_free(TaurusSAXParser* parser) {
    if (parser) {
        free(parser);
    }
}