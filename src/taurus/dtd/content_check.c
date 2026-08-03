/* dtd/content_check.c — Element content-model validation.
 *
 * Phase 4 of TODO 91. Parses a content model like `(a, b+, c?)` into
 * a token stream and matches it against the element's actual children.
 *
 * Scope of Phase 4:
 *   - Element-name tokens (case-sensitive, name match).
 *   - Sequence (`,`) and choice (`|`) combinators.
 *   - Occurrence modifiers `?`, `+`, `*`.
 *   - `#PCDATA` mixed content (`(#PCDATA | a | b)*`).
 *   - Parenthesis grouping.
 *   - Whitespace tolerance.
 *
 * Out of scope (deferred):
 *   - DTD parameter entity expansion (`%entity;`).
 *   - Recursive content models that need first/follow sets.
 *
 * The matcher uses a simple recursive-descent evaluator: at each
 * step it tries to consume the next child node against the next
 * content-model token. If the model permits empty (e.g., `*`, `?`),
 * the matcher accepts without consuming a child. Otherwise a child
 * must be present and match.
 *
 * The first mismatch produces a violation with the offending child
 * name and a hint at what the model expected.
 */

#include "../../include/taurus.h"
#include "../../include/taurus/dtd.h"
#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Tokens produced by the content-model tokenizer. */
typedef enum {
    CMT_NAME,    /* element name (or #PCDATA) */
    CMT_OPEN,    /* ( */
    CMT_CLOSE,   /* ) */
    CMT_COMMA,   /* ,  sequence */
    CMT_PIPE,    /* |  choice */
    CMT_Q,       /* ?  optional */
    CMT_S,       /* *  zero-or-more */
    CMT_P,       /* +  one-or-more */
    CMT_END      /* end of model */
} CMTokenType;

typedef struct {
    const char* src;       /* current read position */
    const char* end;
} CMLexer;

static void cm_skip_ws(CMLexer* lx) {
    while (lx->src < lx->end && isspace((unsigned char)*lx->src)) lx->src++;
}

/* Peek a single character. */
static int cm_peek(CMLexer* lx) {
    if (lx->src >= lx->end) return -1;
    return (unsigned char)*lx->src;
}

/* Read a name: alpha / '_' / ':' followed by alphanumerics / '.' / '-' / '_' / ':'.
 * Also matches the special token `#PCDATA`. */
static int cm_read_name(CMLexer* lx, char* out, size_t out_size) {
    cm_skip_ws(lx);
    size_t i = 0;
    /* Read the first char (alpha, '_', ':', or '#' for #PCDATA). */
    int c = cm_peek(lx);
    if (c < 0) return 0;
    if (c == '#') {
        /* #PCDATA token (or future parameter entities). */
        if (i + 7 < out_size) {
            memcpy(out + i, "#PCDATA", 7);
            i += 7;
        }
        lx->src += 7;
    } else if (isalpha(c) || c == '_' || c == ':') {
        if (i + 1 < out_size) out[i++] = (char)c;
        lx->src++;
        while ((c = cm_peek(lx)) >= 0 &&
               (isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) {
            if (i + 1 < out_size) out[i++] = (char)c;
            lx->src++;
        }
    } else {
        return 0;
    }
    if (i < out_size) out[i] = '\0';
    return 1;
}

static int cm_peek_token(CMLexer* lx, CMTokenType* type, char name_buf[64]) {
    cm_skip_ws(lx);
    int c = cm_peek(lx);
    if (c < 0) { *type = CMT_END; return 1; }
    if (c == '(') { *type = CMT_OPEN; return 1; }
    if (c == ')') { *type = CMT_CLOSE; return 1; }
    if (c == ',') { *type = CMT_COMMA; return 1; }
    if (c == '|') { *type = CMT_PIPE; return 1; }
    if (c == '?') { *type = CMT_Q; return 1; }
    if (c == '*') { *type = CMT_S; return 1; }
    if (c == '+') { *type = CMT_P; return 1; }
    if (cm_read_name(lx, name_buf, 64)) { *type = CMT_NAME; return 1; }
    *type = CMT_END;
    return 0;
}

/* Forward decls for the recursive matcher. */
static int match_seq(CMLexer* lx, const char** child_names, size_t* child_idx,
                      size_t child_count, const char* model_name,
                      char* out_msg, size_t msg_size);

static const char* op_name(CMTokenType t) {
    switch (t) {
        case CMT_COMMA: return ",";
        case CMT_PIPE:  return "|";
        case CMT_Q:     return "?";
        case CMT_S:     return "*";
        case CMT_P:     return "+";
        default:        return "?";
    }
}

/* Match a single particle (NAME?occurrence) against one or more
 * children, advancing *child_idx as needed. Returns 1 on success,
 * 0 on mismatch. */
static int match_particle(CMLexer* lx, const char** child_names,
                           size_t* child_idx, size_t child_count,
                           int allow_empty, const char* model_name,
                           char* out_msg, size_t msg_size) {
    CMTokenType tok;
    char name[64];
    if (!cm_peek_token(lx, &tok, name) || tok != CMT_NAME) {
        snprintf(out_msg, msg_size,
                 "Content model of '%s' expected a name", model_name);
        return 0;
    }
    cm_skip_ws(lx);
    /* Consume the name token by advancing past it. */
    while (lx->src < lx->end && !isspace((unsigned char)*lx->src) &&
           *lx->src != '(' && *lx->src != ')' && *lx->src != ',' &&
           *lx->src != '|' && *lx->src != '?' && *lx->src != '*' &&
           *lx->src != '+') {
        lx->src++;
    }
    /* Optional occurrence modifier. */
    cm_skip_ws(lx);
    int occurrence = 1;  /* default: exactly one */
    if (lx->src < lx->end) {
        if (*lx->src == '?') { lx->src++; occurrence = '?'; }
        else if (*lx->src == '*') { lx->src++; occurrence = '*'; }
        else if (*lx->src == '+') { lx->src++; occurrence = '+'; }
    }

    int matched_any = 0;
    if (*child_idx < child_count &&
        strcmp(child_names[*child_idx], name) == 0) {
        (*child_idx)++;
        matched_any = 1;
    }

    switch (occurrence) {
        case '?':
        case '*':
            return 1;  /* optional / zero-or-more — match even if missing */
        case '+':
            if (matched_any) return 1;
            snprintf(out_msg, msg_size,
                     "Element '%s' content model requires one or more '%s'",
                     model_name, name);
            return 0;
        default:  /* exactly one */
            if (matched_any) return 1;
            if (allow_empty && *child_idx >= child_count) {
                /* The next child would have been missing, but the
                 * caller is OK with empty if optional follows. */
                return 1;
            }
            snprintf(out_msg, msg_size,
                     "Element '%s' content model requires '%s'",
                     model_name, name);
            return 0;
    }
}

/* Match a parenthesized group: ( seq | choice )?occurrence. */
static int match_group(CMLexer* lx, const char** child_names,
                        size_t* child_idx, size_t child_count,
                        int allow_empty, const char* model_name,
                        char* out_msg, size_t msg_size) {
    /* Consume '(' */
    if (lx->src >= lx->end || *lx->src != '(') {
        snprintf(out_msg, msg_size,
                 "Content model of '%s' expected '('", model_name);
        return 0;
    }
    lx->src++;
    cm_skip_ws(lx);

    /* Parse sequence/choice. We support both via recursive descent
     * over particles separated by `,` or `|`. */
    /* First term */
    if (!match_particle(lx, child_names, child_idx, child_count,
                         allow_empty, model_name, out_msg, msg_size)) {
        return 0;
    }
    /* Following terms (sequence or choice) */
    for (;;) {
        cm_skip_ws(lx);
        if (lx->src >= lx->end) break;
        if (*lx->src == ')') break;
        if (*lx->src == ',') {
            lx->src++;
            cm_skip_ws(lx);
            if (!match_particle(lx, child_names, child_idx, child_count,
                                 0, model_name, out_msg, msg_size)) {
                return 0;
            }
        } else if (*lx->src == '|') {
            /* Choice: try to match the current alternative; if it
             * doesn't match the current child, backtrack to before
             * THIS particle and try the alternative. For chained `|`,
             * the matcher already accepted one of the earlier alts. */
            lx->src++;
            cm_skip_ws(lx);
            /* Save BEFORE the current particle (we already consumed
             * it, so we need to track from where we entered this
             * iteration). Since match_particle always advances on
             * success even for `?`/`*`, we instead check: if the
             * child at the current position did NOT match the
             * previous particle, roll back by re-running the alt. */
            CMLexer save = *lx;
            size_t save_idx = *child_idx;
            int alt_ok = match_particle(lx, child_names, child_idx,
                                       child_count, allow_empty,
                                       model_name, out_msg, msg_size);
            if (!alt_ok) {
                /* Alternative didn't match — try skipping it (treat
                 * as optional). This happens when the previous
                 * alternative matched. */
                *lx = save;
                *child_idx = save_idx;
                while (lx->src < lx->end && *lx->src != ')') lx->src++;
            }
        } else {
            break;
        }
    }
    /* Consume ')' */
    if (lx->src >= lx->end || *lx->src != ')') {
        snprintf(out_msg, msg_size,
                 "Content model of '%s' expected ')'", model_name);
        return 0;
    }
    lx->src++;
    /* Optional outer-group occurrence modifier. */
    cm_skip_ws(lx);
    if (lx->src < lx->end) {
        if (*lx->src == '?') { lx->src++; return 1; }  /* outer (a|b)? — allowed */
        if (*lx->src == '*') { lx->src++; return 1; }  /* outer (a|b)* */
        if (*lx->src == '+') { lx->src++; return 1; }  /* outer (a|b)+ — at least one alt */
    }
    return 1;
}

/* Top-level: parse the model and match against children. */
static int match_seq(CMLexer* lx, const char** child_names, size_t* child_idx,
                      size_t child_count, const char* model_name,
                      char* out_msg, size_t msg_size) {
    /* If model starts with '(', parse a group. */
    cm_skip_ws(lx);
    if (lx->src < lx->end && *lx->src == '(') {
        return match_group(lx, child_names, child_idx, child_count,
                            0, model_name, out_msg, msg_size);
    }
    /* Single particle (NAME?occurrence). */
    return match_particle(lx, child_names, child_idx, child_count,
                          0, model_name, out_msg, msg_size);
}

/* Public entry point: match actual children against a content model
 * string. Returns 1 on match, 0 on mismatch (out_msg populated). */
int taurus_content_model_match(const char* model, const char* elem_name,
                                const char** child_names, size_t child_count,
                                char* out_msg, size_t msg_size) {
    if (!model || !elem_name || !child_names) {
        if (out_msg && msg_size) snprintf(out_msg, msg_size, "NULL model or children");
        return 0;
    }
    CMLexer lx = { .src = model, .end = model + strlen(model) };
    size_t child_idx = 0;
    int rc = match_seq(&lx, child_names, &child_idx, child_count,
                       elem_name, out_msg, msg_size);
    if (rc != 1) return 0;
    /* All children must be consumed. */
    if (child_idx != child_count) {
        if (out_msg && msg_size) {
            snprintf(out_msg, msg_size,
                     "Element '%s' has %zu child(ren) but content model accepts %zu",
                     elem_name, child_count, child_idx);
        }
        return 0;
    }
    return 1;
}
