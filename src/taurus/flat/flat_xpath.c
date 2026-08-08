/* flat/flat_xpath.c — FlatDoc-direct XPath dispatch (TODO 145 Phase 3).
 *
 * String-level pattern matching for primitive-returning XPath
 * queries. Recognizes the common "count(//name)" family and routes
 * them to the flat fast path, skipping promote.
 *
 * Why string-level (not AST): the AST cache lives in the regular
 * XPath code path; we'd have to enter it to detect patterns, which
 * defeats the point of bypassing it. A targeted string scan is
 * cheaper and equally correct for these specific patterns.
 */
#include "flat_xpath.h"
#include "flat_fast.h"
#include "flat_doc.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static int starts_with(const char* s, const char* prefix) {
    size_t lp = strlen(prefix);
    return strncmp(s, prefix, lp) == 0;
}

/* Skip whitespace at start. */
static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

/* Parse the inner argument of count(...) / boolean(...) when it
 * looks like "//name" or "//*" or "descendant::name" etc.
 *
 * On match: returns pointer past the parsed arg, fills name_out
 * with a pointer to the name (NULL for wildcard).
 * On no match: returns NULL.
 */
static const char* try_parse_descendant_arg(const char* s,
                                              const char** name_out,
                                              size_t* name_len_out,
                                              int* wildcard_out) {
    *name_out = NULL;
    *name_len_out = 0;
    *wildcard_out = 0;

    s = skip_ws(s);

    /* "//" form. */
    if (s[0] == '/' && s[1] == '/') {
        s += 2;
        if (*s == '*') {
            *wildcard_out = 1;
            return s + 1;
        }
        /* Read name: [a-zA-Z_:][a-zA-Z0-9_.:-]* */
        if (!(isalpha((unsigned char)*s) || *s == '_' || *s == ':')) {
            return NULL;
        }
        const char* start = s;
        while (isalnum((unsigned char)*s) || *s == '_' ||
               *s == ':' || *s == '-' || *s == '.') {
            s++;
        }
        *name_out = start;
        *name_len_out = (size_t)(s - start);
        return s;
    }

    /* "descendant::name" or "descendant-or-self::name" form. */
    if (starts_with(s, "descendant::") ||
        starts_with(s, "descendant-or-self::")) {
        const char* arrow = strstr(s, "::");
        s = arrow + 2;
        if (*s == '*') {
            *wildcard_out = 1;
            return s + 1;
        }
        if (!(isalpha((unsigned char)*s) || *s == '_' || *s == ':')) {
            return NULL;
        }
        const char* start = s;
        while (isalnum((unsigned char)*s) || *s == '_' ||
               *s == ':' || *s == '-' || *s == '.') {
            s++;
        }
        *name_out = start;
        *name_len_out = (size_t)(s - start);
        return s;
    }

    return NULL;
}

int flat_xpath_try_eval(struct taurus_document* doc,
                         const char* expression,
                         struct taurus_xpath_result** out_result) {
    if (!doc || !doc->flat_doc || doc->flat_promoted || !expression) {
        return 0;
    }

    const char* p = skip_ws(expression);

    /* Pattern: count(//name) or count(//*) or count(descendant::name).
     *
     * IMPORTANT (issue #201): after the closing ')', verify the
     * expression is complete. count(//book) > 0 starts with count(
     * but is a comparison, not a standalone count() call. We must
     * NOT match it — the compact XPath path handles comparisons
     * correctly. */
    if (starts_with(p, "count(")) {
        const char* after_open = p + 6;
        const char* name; size_t name_len; int wildcard;
        const char* after_arg = try_parse_descendant_arg(
            after_open, &name, &name_len, &wildcard);
        if (!after_arg) return 0;
        after_arg = skip_ws(after_arg);
        if (*after_arg != ')') return 0;
        /* Verify nothing follows the ')' — full expression match. */
        const char* after_close = skip_ws(after_arg + 1);
        if (*after_close != '\0') return 0;

        size_t count;
        if (wildcard) {
            count = flat_fast_count_elements_all(doc);
        } else {
            /* Copy name to NUL-terminated buffer for the helper. */
            char name_buf[256];
            if (name_len >= sizeof(name_buf)) return 0;
            memcpy(name_buf, name, name_len);
            name_buf[name_len] = '\0';
            count = flat_fast_count_elements_named(doc, name_buf);
        }

        struct taurus_xpath_result* r = (struct taurus_xpath_result*)
            malloc(sizeof(*r));
        if (!r) return 0;
        r->type = XPATH_RESULT_NUMBER;
        r->value.number_value = (double)count;
        *out_result = r;
        return 1;
    }

    /* Pattern: boolean(//name) — true if any matches */
    if (starts_with(p, "boolean(")) {
        const char* after_open = p + 8;
        const char* name; size_t name_len; int wildcard;
        const char* after_arg = try_parse_descendant_arg(
            after_open, &name, &name_len, &wildcard);
        if (!after_arg) return 0;
        after_arg = skip_ws(after_arg);
        if (*after_arg != ')') return 0;
        /* Verify nothing follows the ')' (issue #201). */
        const char* after_close = skip_ws(after_arg + 1);
        if (*after_close != '\0') return 0;

        int found = 0;
        if (wildcard) {
            found = flat_fast_count_elements_all(doc) > 0;
        } else {
            char name_buf[256];
            if (name_len >= sizeof(name_buf)) return 0;
            memcpy(name_buf, name, name_len);
            name_buf[name_len] = '\0';
            found = flat_fast_count_elements_named(doc, name_buf) > 0;
        }

        struct taurus_xpath_result* r = (struct taurus_xpath_result*)
            malloc(sizeof(*r));
        if (!r) return 0;
        r->type = XPATH_RESULT_BOOLEAN;
        r->value.boolean_value = found;
        *out_result = r;
        return 1;
    }

    return 0;
}
