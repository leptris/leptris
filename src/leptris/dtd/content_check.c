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

#include "../../include/leptris.h"
#include "../../include/leptris/dtd.h"
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

/* ---- the matcher (TODO.hygiene/02 closeout) --------------------------
 *
 * A backtracking recursive-descent evaluator over the model with the
 * child list as input stream. State = (lexer position, child index);
 * every trial saves/restores both, so choices and repetitions reject
 * a branch without consuming children:
 *
 *   expr     := sequence ('|' sequence)*        — first match wins
 *   sequence := term (',' term)*
 *   term     := ( NAME | '(' expr ')' ) occurrence?
 *   occurrence := '' | '?' | '*' | '+'
 *
 * Mixed models (`(#PCDATA | a | b)*`) are validated separately: the
 * alternation names form a SET, every element child must belong to
 * it, text is always allowed; `(#PCDATA)` without `*` allows no
 * element children at all (XML 1.0 §3.2.2, the mixed grammar). */

typedef struct {
    const char** names;   /* element children only */
    size_t count;
    size_t idx;
    CMLexer lx;
    const char* model_name;
    char* out_msg;
    size_t msg_size;
} CMState;

static int cm_match_expr(CMState* st);

/* Match ONE instance of the group whose '(' sits at group_start.
 * On success the lexer lands just past the group's ')'; on failure
 * the state is restored. */
static int cm_try_group_once(CMState* st, const char* group_start) {
    CMLexer save_lx = st->lx;
    size_t save_idx = st->idx;

    st->lx.src = group_start + 1;  /* past '(' */
    if (!cm_match_expr(st)) goto fail;
    cm_skip_ws(&st->lx);
    if (st->lx.src >= st->lx.end || *st->lx.src != ')') {
        if (st->out_msg && st->msg_size)
            snprintf(st->out_msg, st->msg_size,
                     "Content model of '%s' expected ')'",
                     st->model_name);
        goto fail;
    }
    st->lx.src++;
    return 1;
fail:
    st->lx = save_lx;
    st->idx = save_idx;
    return 0;
}

/* term := ( NAME | '(' expr ')' ) occurrence? */
static int cm_match_term(CMState* st) {
    cm_skip_ws(&st->lx);
    int c = cm_peek(&st->lx);
    if (c < 0) return 0;

    if (c == '(') {
        const char* group_start = st->lx.src;
        /* occurrence needs the token AFTER ')': peek the balanced
         * close by trying once first — the occurrence layer below
         * re-enters via cm_try_group_once, so this match consumes
         * exactly one instance. */
        if (!cm_try_group_once(st, group_start)) return 0;
        cm_skip_ws(&st->lx);
        char occurrence = 1;
        if (st->lx.src < st->lx.end) {
            if (*st->lx.src == '?') { st->lx.src++; occurrence = '?'; }
            else if (*st->lx.src == '*') { st->lx.src++; occurrence = '*'; }
            else if (*st->lx.src == '+') { st->lx.src++; occurrence = '+'; }
        }
        if (occurrence == 1 || occurrence == '?') return 1;
        /* '*' / '+': trial further instances while they match and
         * make progress (a zero-width body terminates the loop) */
        for (;;) {
            CMLexer save_lx = st->lx;
            size_t save_idx = st->idx;
            if (!cm_try_group_once(st, group_start)) {
                st->lx = save_lx;   /* the trailing occurrence token? no —
                                       try_group_once restores to before
                                       the retry; keep the successful
                                       state from before this trial */
                st->lx = save_lx;
                st->idx = save_idx;
                return 1;
            }
            if (st->idx == save_idx) {
                /* zero-width instance: stop, state stays at the last
                 * successful position (already equal) */
                return 1;
            }
        }
    }

    if (c == '#') {
        /* #PCDATA handled by the mixed path; a bare one here is a
         * malformed model for element-only matching */
        if (st->out_msg && st->msg_size)
            snprintf(st->out_msg, st->msg_size,
                     "Content model of '%s' mixes #PCDATA with element "
                     "content", st->model_name);
        return 0;
    }

    char name[64];
    CMTokenType tok;
    if (!cm_peek_token(&st->lx, &tok, name) || tok != CMT_NAME) {
        if (st->out_msg && st->msg_size)
            snprintf(st->out_msg, st->msg_size,
                     "Content model of '%s' expected a name",
                     st->model_name);
        return 0;
    }
    while (st->lx.src < st->lx.end &&
           !isspace((unsigned char)*st->lx.src) &&
           *st->lx.src != '(' && *st->lx.src != ')' &&
           *st->lx.src != ',' && *st->lx.src != '|' &&
           *st->lx.src != '?' && *st->lx.src != '*' &&
           *st->lx.src != '+') {
        st->lx.src++;
    }
    cm_skip_ws(&st->lx);
    char occurrence = 1;
    if (st->lx.src < st->lx.end) {
        if (*st->lx.src == '?') { st->lx.src++; occurrence = '?'; }
        else if (*st->lx.src == '*') { st->lx.src++; occurrence = '*'; }
        else if (*st->lx.src == '+') { st->lx.src++; occurrence = '+'; }
    }

    size_t matched = 0;
    while (st->idx < st->count &&
           strcmp(st->names[st->idx], name) == 0) {
        st->idx++;
        matched++;
        if (occurrence == 1 || occurrence == '?') break;
    }
    switch (occurrence) {
        case '?':
        case '*':
            return 1;
        case '+':
            if (matched) return 1;
            if (st->out_msg && st->msg_size)
                snprintf(st->out_msg, st->msg_size,
                         "Element '%s' content model requires one or "
                         "more '%s'", st->model_name, name);
            return 0;
        default:
            if (matched) return 1;
            if (st->out_msg && st->msg_size)
                snprintf(st->out_msg, st->msg_size,
                         "Element '%s' content model requires '%s'",
                         st->model_name, name);
            return 0;
    }
}

/* sequence := term (',' term)* */
static int cm_match_sequence(CMState* st) {
    if (!cm_match_term(st)) return 0;
    for (;;) {
        cm_skip_ws(&st->lx);
        if (st->lx.src >= st->lx.end || *st->lx.src != ',') return 1;
        st->lx.src++;
        if (!cm_match_term(st)) return 0;
    }
}

/* Lexically walk past one unmatched alternative (balanced parens,
 * stopping at the depth-0 '|' or ')'). */
static void cm_skip_alternative(CMLexer* lx) {
    int depth = 0;
    while (lx->src < lx->end) {
        char c = *lx->src;
        if (c == '(') depth++;
        else if (c == ')') {
            if (depth == 0) return;
            depth--;
        } else if (c == '|' && depth == 0) return;
        lx->src++;
    }
}

/* expr := sequence ('|' sequence)* — first match wins; failed
 * alternatives restore the state, and after a SUCCESS the remaining
 * alternatives are skipped lexically so the enclosing group's ')'
 * check lands on the real close. */
static int cm_match_expr(CMState* st) {
    CMLexer save_lx = st->lx;
    size_t save_idx = st->idx;
    if (cm_match_sequence(st)) {
        for (;;) {
            cm_skip_ws(&st->lx);
            if (st->lx.src >= st->lx.end || *st->lx.src != '|') break;
            st->lx.src++;
            cm_skip_alternative(&st->lx);
        }
        return 1;
    }
    for (;;) {
        cm_skip_ws(&st->lx);
        if (st->lx.src >= st->lx.end || *st->lx.src != '|') {
            st->lx = save_lx;
            st->idx = save_idx;
            return 0;
        }
        st->lx.src++;
        if (cm_match_sequence(st)) {
            for (;;) {
                cm_skip_ws(&st->lx);
                if (st->lx.src >= st->lx.end || *st->lx.src != '|') break;
                st->lx.src++;
                cm_skip_alternative(&st->lx);
            }
            return 1;
        }
        /* this alternative failed — restore and try the next one */
    }
}

/* Mixed model: `(#PCDATA)` or `(#PCDATA|n1|n2...)*`. */
static int cm_match_mixed(const char* model, CMState* st) {
    /* collect the allowed element names between '(' and ')' */
    struct { const char* s; size_t len; } names[64];
    size_t n = 0;
    const char* p = strchr(model, '(');
    const char* close = strrchr(model, ')');
    if (!p || !close || close < p) {
        if (st->out_msg && st->msg_size)
            snprintf(st->out_msg, st->msg_size,
                     "Content model of '%s' is malformed",
                     st->model_name);
        return 0;
    }
    int star = 0;
    {
        const char* q = close + 1;
        while (*q && isspace((unsigned char)*q)) q++;
        star = (*q == '*');
    }
    p++;
    while (p < close) {
        while (p < close && (isspace((unsigned char)*p) || *p == '|')) p++;
        if (p >= close) break;
        const char* s = p;
        while (p < close && *p != '|' && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - s);
        if (len == 7 && strncmp(s, "#PCDATA", 7) == 0) continue;
        if (n < 64) {
            /* borrowed (ptr,len): model text is not NUL-terminated
             * at the name boundary */
            names[n].s = s;
            names[n].len = len;
            n++;
        }
    }
    for (size_t i = 0; i < st->count; i++) {
        int allowed = 0;
        for (size_t j = 0; j < n; j++) {
            if (strlen(st->names[i]) == names[j].len &&
                strncmp(st->names[i], names[j].s, names[j].len) == 0) {
                allowed = 1;
                break;
            }
        }
        if (!allowed) {
            if (st->out_msg && st->msg_size)
                snprintf(st->out_msg, st->msg_size,
                         "Element '%s' child '%s' is not allowed by the "
                         "mixed content model", st->model_name,
                         st->names[i]);
            return 0;
        }
    }
    if (!star && st->count > 0) {
        if (st->out_msg && st->msg_size)
            snprintf(st->out_msg, st->msg_size,
                     "Element '%s' mixed content model allows no element "
                     "children", st->model_name);
        return 0;
    }
    st->idx = st->count;
    return 1;
}

/* Public entry point: match actual children against a content model
 * string. Returns 1 on match, 0 on mismatch (out_msg populated). */
int leptris_content_model_match(const char* model, const char* elem_name,
                                const char** child_names, size_t child_count,
                                char* out_msg, size_t msg_size) {
    if (!model || !elem_name || !child_names) {
        if (out_msg && msg_size) snprintf(out_msg, msg_size, "NULL model or children");
        return 0;
    }
    CMState st = {0};
    st.names = child_names;
    st.count = child_count;
    st.idx = 0;
    st.lx = (CMLexer){ .src = model, .end = model + strlen(model) };
    st.model_name = elem_name;
    st.out_msg = out_msg;
    st.msg_size = msg_size;

    if (strstr(model, "#PCDATA")) return cm_match_mixed(model, &st);

    if (!cm_match_expr(&st)) {
        if (out_msg && msg_size && out_msg[0] == 0) {
            snprintf(out_msg, msg_size,
                     "Element '%s' does not match its content model",
                     elem_name);
        }
        return 0;
    }
    if (st.idx != st.count) {
        if (out_msg && msg_size) {
            snprintf(out_msg, msg_size,
                     "Element '%s' has %zu child(ren) but the content "
                     "model accepts %zu",
                     elem_name, child_count, st.idx);
        }
        return 0;
    }
    return 1;
}
