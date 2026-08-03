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
 *
 * The scratch buffer is a small growable arena used to materialize
 * NUL-terminated copies of names and attribute values for the
 * callbacks.  It replaces a per-name malloc/free pair that dominated
 * SAX throughput — see TODO 102.  The buffer is reset at the start of
 * each top-level call; callbacks receive pointers into it that are
 * valid only for the duration of the callback.
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

    /* Scratch arena for transient name/value copies (TODO 102). */
    char*  scratch;
    size_t scratch_len;
    size_t scratch_cap;
};

/* ============================================================================
 * Parser Utilities
 * ============================================================================ */

static inline int sax_at_end(TaurusSAXParser* p) {
    return p->pos >= p->end;
}

static inline char sax_peek(TaurusSAXParser* p) {
    if (sax_at_end(p)) return '\0';
    return *p->pos;
}

/* Advance one byte and track line/column.  Hot loops that don't need
 * line numbers (whitespace runs, name scans, body text) use direct
 * pointer arithmetic instead. */
static inline char sax_advance(TaurusSAXParser* p) {
    if (sax_at_end(p)) return '\0';
    char c = *p->pos++;
    if (c == '\n') {
        p->line++;
        p->column = 1;
    } else {
        p->column++;
    }
    return c;
}

static inline int sax_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Skip a whitespace run without per-char line tracking.  The run is
 * almost always short (single space between attrs, newline + indent
 * between elements), so the line/column recompute is cheap. */
static void sax_skip_whitespace(TaurusSAXParser* p) {
    const char* start = p->pos;
    const char* end = p->end;
    const char* s = start;
    while (s < end) {
        char c = *s;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        s++;
    }
    if (s == start) return;

    ptrdiff_t consumed = s - start;
    int nl_count = 0;
    const char* last_nl = NULL;
    for (const char* q = start; q < s; q++) {
        if (*q == '\n') { nl_count++; last_nl = q; }
    }
    p->line += nl_count;
    if (last_nl) {
        p->column = (int)(s - last_nl);
    } else {
        p->column += (int)consumed;
    }
    p->pos = s;
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

/* Ensure the scratch arena has at least `cap` bytes free; returns the
 * start of the writable region (no allocation is consumed yet — the
 * caller advances scratch_len after writing).  Returns NULL on OOM. */
static char* sax_scratch_reserve(TaurusSAXParser* p, size_t cap) {
    if (p->scratch_len + cap <= p->scratch_cap) {
        return p->scratch + p->scratch_len;
    }
    size_t need = p->scratch_len + cap;
    size_t new_cap = p->scratch_cap ? p->scratch_cap : 256;
    while (new_cap < need) new_cap *= 2;
    char* grown = (char*)realloc(p->scratch, new_cap);
    if (!grown) {
        sax_set_error(p, "out of memory");
        return NULL;
    }
    p->scratch = grown;
    p->scratch_cap = new_cap;
    return p->scratch + p->scratch_len;
}

/* Append [start, start+len) to scratch and return a NUL-terminated
 * pointer to the in-arena copy.  Does NOT advance the input position. */
static const char* sax_scratch_append(TaurusSAXParser* p,
                                       const char* start, size_t len) {
    char* dst = sax_scratch_reserve(p, len + 1);
    if (!dst) return NULL;
    if (len) memcpy(dst, start, len);
    dst[len] = '\0';
    const char* result = dst;
    p->scratch_len += len + 1;
    return result;
}

/**
 * Parse element name.  The result is a parser-scratch pointer; valid
 * until the next call into the parser.  Advances p->pos past the name.
 */
static const char* sax_parse_name(TaurusSAXParser* p) {
    const char* start = p->pos;

    if (start >= p->end || !sax_is_name_start(*start)) {
        sax_set_error(p, "Expected element name");
        return NULL;
    }

    /* Hot loop: scan name chars with no per-char function call.
     * The compiler inlines sax_is_name_char, but the explicit loop
     * also lets the autovectorizer see the comparison pattern. */
    const char* s = start + 1;
    while (s < p->end && sax_is_name_char(*s)) s++;

    size_t len = (size_t)(s - start);
    p->pos = s;
    return sax_scratch_append(p, start, len);
}

/**
 * Parse attribute value.  Same scratch-arena contract as sax_parse_name.
 */
static const char* sax_parse_attr_value(TaurusSAXParser* p) {
    if (p->pos >= p->end) {
        sax_set_error(p, "Expected quote for attribute value");
        return NULL;
    }
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') {
        sax_set_error(p, "Expected quote for attribute value");
        return NULL;
    }
    p->pos++; /* skip opening quote */

    const char* start = p->pos;
    /* memchr is vectorized on modern CPUs — far faster than the
     * per-character scan the old version did. */
    const char* found = (const char*)memchr(start, quote, (size_t)(p->end - start));
    if (!found) {
        /* Runaway value — consume rest of input but report parse error. */
        size_t len = (size_t)(p->end - start);
        p->pos = p->end;
        sax_set_error(p, "Unterminated attribute value");
        return sax_scratch_append(p, start, len);
    }
    size_t len = (size_t)(found - start);
    p->pos = found + 1; /* skip closing quote */
    return sax_scratch_append(p, start, len);
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
    const char* name = sax_parse_name(p);
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

            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                }
                free(attrs);
            }
            return 0;
        }

        /* Parse attribute name */
        const char* attr_name = sax_parse_name(p);
        if (!attr_name) {
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                }
                free(attrs);
            }
            return -1;
        }

        sax_skip_whitespace(p);

        /* Expect '=' */
        if (sax_peek(p) != '=') {
            sax_set_error(p, "Expected '=' after attribute name");
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
                }
                free(attrs);
            }
            return -1;
        }
        sax_advance(p);

        sax_skip_whitespace(p);

        /* Parse attribute value */
        const char* attr_value = sax_parse_attr_value(p);
        if (!attr_value) {
            if (attrs) {
                for (size_t i = 0; i < attr_count * 2; i++) {
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
                if (attrs) {
                    for (size_t i = 0; i < attr_count * 2; i++) {
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
            const char* close_name = sax_parse_name(p);
            if (!close_name || strcmp(close_name, name) != 0) {
                sax_set_error(p, "Mismatched closing tag");
                if (attrs) {
                    for (size_t i = 0; i < attr_count * 2; i++) {
                    }
                    free(attrs);
                }
                return -1;
            }

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
                    if (attrs) {
                        for (size_t i = 0; i < attr_count * 2; i++) {
                        }
                        free(attrs);
                    }
                    return -1;
                }
            }
        } else {
            /* Text content.  memchr is vectorized; far faster than the
             * per-character sax_peek/sax_advance loop the old code used. */
            const char* start = p->pos;
            const char* found = (p->pos < p->end)
                ? (const char*)memchr(p->pos, '<', (size_t)(p->end - p->pos))
                : NULL;
            p->pos = found ? found : p->end;
            size_t len = (size_t)(p->pos - start);
            if (len > 0) {
                /* Update line/column for the skipped bytes — count newlines. */
                int nl_count = 0;
                const char* last_nl = NULL;
                for (const char* q = start; q < p->pos; q++) {
                    if (*q == '\n') { nl_count++; last_nl = q; }
                }
                p->line += nl_count;
                if (last_nl) p->column = (int)(p->pos - last_nl);
                else         p->column += (int)len;

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

    if (attrs) {
        for (size_t i = 0; i < attr_count * 2; i++) {
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

    /* Pre-size scratch arena to doc size so it never needs to realloc
     * mid-parse (which would invalidate pointers held across nested
     * elements).  Total name + attribute-value bytes can never exceed
     * doc bytes; +1 for a trailing NUL. */
    parser.scratch_cap = len + 1;
    parser.scratch = (char*)malloc(parser.scratch_cap);
    if (!parser.scratch) return -1;
    parser.scratch_len = 0;

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
    int rc = sax_parse_element(&parser);
    free(parser.scratch);
    if (rc < 0) {
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
    parser->scratch = NULL;
    parser->scratch_len = 0;
    parser->scratch_cap = 0;

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
        free(parser->scratch);
        free(parser);
    }
}