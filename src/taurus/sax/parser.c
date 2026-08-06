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

    /* Incremental parsing support (TODO 89).
     * input_buf accumulates chunks from taurus_sax_parser_feed
     * calls. The parser works on [pos, end) within this buffer.
     * On each feed call, new data is appended and we try to
     * parse as far as we safely can (stopping before any
     * construct whose closing delimiter hasn't arrived yet). */
    char*  input_buf;
    size_t input_len;
    size_t input_cap;
    int    document_started;  /* start_document emitted */
    int    document_ended;    /* end_document emitted */
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

/* ============================================================================
 * Chartype lookup table (TODO 107 — pugixml-style speed trick)
 *
 * One byte load + one AND + one branch replaces the multi-comparison
 * patterns like `c == ' ' || c == '\t' || c == '\n' || c == '\r'`.
 * pugixml uses exactly this trick; see TODO 107.
 *
 * Bits:
 *   1  = space        (\r \n space tab)
 *   2  = name start   (a-z A-Z _ :)
 *   4  = name char    (a-z A-Z 0-9 _ : - .)
 *   8  = digit        (0-9)
 *   16 = quote        (' ")
 * ============================================================================ */

#define SAX_CT_SPACE       1
#define SAX_CT_NAME_START  2
#define SAX_CT_NAME_CHAR   4
#define SAX_CT_DIGIT       8
#define SAX_CT_QUOTE       16

static const unsigned char sax_chartype_table[256] = {
    /*       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    /* 0 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
    /* 1 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2 */  1, 0, 16, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 4, 4, 0,
    /*       SP !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /    */
    /* 3 */ 12,12,12,12,12,12,12,12,12,12, 4, 0, 0, 0, 0, 0,
    /*       0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?    */
    /* 4 */  0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /*       @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O    */
    /* 5 */  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 6,
    /*       P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _    */
    /* 6 */  0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /*       `  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o    */
    /* 7 */  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 0,
    /*       p  q  r  s  t  u  v  w  x  y  z  {  |  }  ~  DEL  */
    /* 8 */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* 9 */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* A */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* B */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* C */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* D */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* E */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* F */  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

static inline int sax_is_whitespace(char c) {
    return (sax_chartype_table[(unsigned char)c] & SAX_CT_SPACE) != 0;
}

static inline int sax_is_name_start(char c) {
    return (sax_chartype_table[(unsigned char)c] & SAX_CT_NAME_START) != 0;
}

static inline int sax_is_name_char(char c) {
    return (sax_chartype_table[(unsigned char)c] & SAX_CT_NAME_CHAR) != 0;
}

static inline int sax_is_quote(char c) {
    return (sax_chartype_table[(unsigned char)c] & SAX_CT_QUOTE) != 0;
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

    /* Hot loop with manual 4-way unroll (TODO 107 — pugixml trick #2:
     * SCANWHILE_UNROLL).  Cuts loop-overhead (counter increment +
     * branch) by 4x.  Stops at end-of-input or first non-name-char. */
    const char* s = start + 1;
    const char* end = p->end;
    for (;;) {
        if (s >= end || !sax_is_name_char(s[0])) break;
        if (s + 1 >= end || !sax_is_name_char(s[1])) { s += 1; break; }
        if (s + 2 >= end || !sax_is_name_char(s[2])) { s += 2; break; }
        if (s + 3 >= end || !sax_is_name_char(s[3])) { s += 3; break; }
        s += 4;
    }

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
    if (!sax_is_quote(quote)) {
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
        /* Closing tag — check directly instead of via sax_match. */
        if (p->pos[0] == '<' && p->pos + 1 < p->end && p->pos[1] == '/') {
            /* Skip "</" */
            p->pos += 2;
            /* Update line/column — '<' and '/' are never '\n'. */
            p->column += 2;

            /* Closing tag name: it must match the opening name.  Skip
             * the scratch-arena round-trip and compare directly against
             * the input.  We know `name` came from scratch; we know its
             * length from when we parsed it (re-derive via strlen —
             * cheap, names are short). */
            size_t name_len = strlen(name);
            int matched = (p->pos + name_len <= p->end &&
                           memcmp(p->pos, name, name_len) == 0 &&
                           (p->pos + name_len == p->end ||
                            sax_is_whitespace(p->pos[name_len]) ||
                            p->pos[name_len] == '>'));

            if (!matched) {
                sax_set_error(p, "Mismatched closing tag");
                if (attrs) {
                    free(attrs);
                }
                return -1;
            }
            p->pos += name_len;
            p->column += (int)name_len;

            /* Skip whitespace + expect '>'. */
            while (p->pos < p->end) {
                char c = *p->pos;
                if (c == '>') { p->pos++; p->column++; break; }
                if (c == '\n') { p->line++; p->column = 1; p->pos++; }
                else if (c == ' ' || c == '\t' || c == '\r') { p->pos++; p->column++; }
                else break;
            }

            break; /* End of element */
        }

        /* Check for child element.  Dispatch on the byte after '<' to
         * avoid the 1-5 strncmp calls per body iteration the old
         * sax_match() chain did. */
        if (sax_peek(p) == '<') {
            const char* tag_start = p->pos;
            char kind = (p->pos + 1 < p->end) ? p->pos[1] : '\0';

            if (kind == '!' && p->pos + 3 < p->end &&
                p->pos[2] == '-' && p->pos[3] == '-') {
                /* Comment.  Find "-->" by scanning for '-' then checking
                 * the next two bytes — far faster than per-char sax_match. */
                p->pos += 4;
                const char* start = p->pos;
                const char* found = NULL;
                while (p->pos < p->end) {
                    const char* dash = (const char*)memchr(p->pos, '-', (size_t)(p->end - p->pos));
                    if (!dash) { p->pos = p->end; break; }
                    if (dash + 2 < p->end && dash[1] == '-' && dash[2] == '>') {
                        found = dash;
                        break;
                    }
                    p->pos = dash + 1;
                }
                size_t len = found ? (size_t)(found - start) : (size_t)(p->pos - start);

                if (p->handler && p->handler->comment && len > 0) {
                    const char* comment = sax_scratch_append(p, start, len);
                    if (comment) p->handler->comment(p->user_data, comment);
                }
                if (found) p->pos = found + 3;
            } else if (kind == '!' && p->pos + 8 < p->end &&
                       p->pos[2] == '[' && p->pos[3] == 'C' &&
                       p->pos[4] == 'D' && p->pos[5] == 'A' &&
                       p->pos[6] == 'T' && p->pos[7] == 'A' &&
                       p->pos[8] == '[') {
                /* CDATA.  Find "]]>" via memchr for ']'. */
                p->pos += 9;
                const char* start = p->pos;
                const char* found = NULL;
                while (p->pos < p->end) {
                    const char* bracket = (const char*)memchr(p->pos, ']', (size_t)(p->end - p->pos));
                    if (!bracket) { p->pos = p->end; break; }
                    if (bracket + 2 < p->end && bracket[1] == ']' && bracket[2] == '>') {
                        found = bracket;
                        break;
                    }
                    p->pos = bracket + 1;
                }
                size_t len = found ? (size_t)(found - start) : (size_t)(p->pos - start);

                if (p->handler && p->handler->cdata && len > 0) {
                    const char* cdata = sax_scratch_append(p, start, len);
                    if (cdata) p->handler->cdata(p->user_data, cdata);
                }
                if (found) p->pos = found + 3;
            } else if (kind == '?') {
                /* Processing Instruction. */
                p->pos += 2; /* skip "<?" */

                /* Parse PI target via direct scan. */
                const char* target_start = p->pos;
                while (p->pos < p->end) {
                    char c = *p->pos;
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '?') break;
                    p->pos++;
                }
                size_t target_len = (size_t)(p->pos - target_start);
                const char* target = sax_scratch_append(p, target_start, target_len);

                /* Skip optional whitespace. */
                while (p->pos < p->end) {
                    char c = *p->pos;
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
                    p->pos++;
                }

                /* PI data runs to "?>".  memchr for '?' then check '>'. */
                const char* data_start = p->pos;
                const char* data_end = p->end;
                while (p->pos < p->end) {
                    const char* q = (const char*)memchr(p->pos, '?', (size_t)(p->end - p->pos));
                    if (!q) { p->pos = p->end; break; }
                    if (q + 1 < p->end && q[1] == '>') {
                        data_end = q;
                        break;
                    }
                    p->pos = q + 1;
                }
                size_t data_len = (size_t)(data_end - data_start);
                const char* data = sax_scratch_append(p, data_start, data_len);

                if (p->handler && p->handler->processing_instruction) {
                    p->handler->processing_instruction(p->user_data, target, data);
                }
                if (data_end < p->end) p->pos = data_end + 2;  /* skip "?>" */
                (void)tag_start;
            } else {
                /* Child element. */
                if (sax_parse_element(p) < 0) {
                    if (attrs) {
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
 * Feed XML chunk (incremental parsing).
 *
 * Appends `xml[0..len)` to the parser's internal buffer. When
 * `is_final` is true, the entire buffered document is parsed and
 * events are emitted.
 *
 * Non-final calls simply accumulate data in the buffer. This bounds
 * memory to the document size, which is correct for the common
 * "large document split across network packets" pattern. True
 * streaming (events emitted before the full document arrives)
 * requires converting the recursive-descent parser to an explicit
 * state machine — see TODO 89 for the full plan.
 *
 * Returns 0 on success, -1 on parse error (only possible on the
 * final chunk).
 */
int taurus_sax_parser_feed(TaurusSAXParser* parser,
                            const char* xml,
                            size_t len,
                            int is_final) {
    if (!parser || !xml) return -1;

    /* Append chunk to the internal buffer. */
    if (len > 0) {
        if (parser->input_len + len > parser->input_cap) {
            size_t new_cap = parser->input_cap == 0 ? 256 : parser->input_cap;
            while (new_cap < parser->input_len + len) new_cap *= 2;
            char* new_buf = (char*)realloc(parser->input_buf, new_cap);
            if (!new_buf) return -1;
            parser->input_buf = new_buf;
            parser->input_cap = new_cap;
        }
        memcpy(parser->input_buf + parser->input_len, xml, len);
        parser->input_len += len;
    }

    if (!is_final) return 0;

    /* Final chunk: parse the complete buffered document. */
    if (parser->input_len == 0) return 0;

    int rc = taurus_sax_parse(parser->input_buf, parser->input_len,
                               parser->handler, parser->user_data);

    /* Free the buffer; the parse is complete. */
    free(parser->input_buf);
    parser->input_buf = NULL;
    parser->input_len = 0;
    parser->input_cap = 0;

    return rc;
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