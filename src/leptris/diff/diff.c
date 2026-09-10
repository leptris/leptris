/* diff/diff.c — native XML diff (lane 17).
 *
 * Digest-pruned ordered edit script: equal subtrees (content-
 * defined Merkle digest, #869) prune in O(1); diverging child
 * lists align with an LCS over digests, then same-named elements
 * recurse. Consumer of record: the user's lutaml/canon gem.
 */

#include "../../include/leptris.h"
#include "../leptris_internal.h"
#include "../dom/element.h"
#include "../dom/text.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DIFF_MAX_CHILDREN 8192

typedef struct {
    int type;
    char* path;
    char* name;
    char* before;
    char* after;
} DiffOp;

struct leptris_diff {
    DiffOp* ops;
    size_t n;
    size_t cap;
};

static char* d_strdup(const char* s) {
    return leptris_strdup(s ? s : "");
}

static void d_push(struct leptris_diff* d, int type, const char* path,
                   const char* name, const char* before,
                   const char* after) {
    if (d->n == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 16;
        DiffOp* no = (DiffOp*)realloc(d->ops, nc * sizeof(DiffOp));
        if (!no) return;
        d->ops = no;
        d->cap = nc;
    }
    DiffOp* o = &d->ops[d->n++];
    o->type = type;
    o->path = d_strdup(path);
    o->name = d_strdup(name);
    o->before = d_strdup(before);
    o->after = d_strdup(after);
}

/* ---- path building: /name[k] among same-named element siblings */

static void d_child_path(char* buf, size_t cap, const char* parent,
                         LeptrisNodeRef child, int force_index) {
    const char* nm = "";
    if (leptris_node_get_type(child) == LEPTRIS_NODE_TYPE_ELEMENT)
        nm = leptris_element_name((LeptrisElement)child);
    size_t same = 0, idx = 0;
    LeptrisElement p = leptris_node_parent(child);
    for (LeptrisNodeRef c =
             leptris_node_first_child((LeptrisNodeRef)p);
         c;
         c = leptris_node_get_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        if (strcmp(leptris_element_name((LeptrisElement)c), nm) == 0) {
            same++;
            if (c == child) idx = same;
        }
    }
    if (force_index || same > 1)
        snprintf(buf, cap, "%s/%s[%zu]", parent, nm, idx);
    else
        snprintf(buf, cap, "%s/%s", parent, nm);
}

/* ---- element diff */

static int d_ws_only(const char* s) {
    if (!s) return 1;
    for (const char* p = s; *p; p++)
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            return 0;
    return 1;
}

static void d_diff_element(struct leptris_diff* d, LeptrisElement a,
                           LeptrisElement b, const char* path,
                           LeptrisDiffFlags flags) {
    /* Digest fast path: equal subtree, nothing to report. */
    LeptrisDigestFlags df =
        (flags & LEPTRIS_DIFF_IGNORE_WS_TEXT)
            ? LEPTRIS_DIGEST_DROP_WS_TEXT
            : LEPTRIS_DIGEST_DEFAULT;
    if (leptris_node_digest((LeptrisNodeRef)a, df) ==
        leptris_node_digest((LeptrisNodeRef)b, df))
        return;

    /* Attributes: removals + changes from A's walk, then
     * additions from B's walk (document order each). */
    for (struct leptris_attribute* at =
             leptris_element_get_first_attribute(a);
         at; at = leptris_attr_next(at)) {
        const char* an = attr_cname(at);
        const char* av = attr_cvalue(at);
        const char* bv = NULL;
        for (struct leptris_attribute* bt =
                 leptris_element_get_first_attribute(b);
             bt; bt = leptris_attr_next(bt)) {
            if (strcmp(attr_cname(bt), an) == 0) {
                bv = attr_cvalue(bt);
                break;
            }
        }
        if (!bv) {
            d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, an, av, "");
        } else if (strcmp(av, bv) != 0) {
            d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, an, av, bv);
        }
    }
    for (struct leptris_attribute* bt =
             leptris_element_get_first_attribute(b);
         bt; bt = leptris_attr_next(bt)) {
        const char* bn = attr_cname(bt);
        int found = 0;
        for (struct leptris_attribute* at =
                 leptris_element_get_first_attribute(a);
             at; at = leptris_attr_next(at)) {
            if (strcmp(attr_cname(at), bn) == 0) {
                found = 1;
                break;
            }
        }
        if (!found)
            d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, bn, "",
                   attr_cvalue(bt));
    }

    /* Namespace declarations: prefix->URI pairs on THIS element.
     * Differences surface as UPDATE_ATTR ops on the xmlns names —
     * the op model used to be namespace-blind (a changed URI
     * reported "identical"; the digest saw it, the reporting
     * didn't — found via canon's namespace specs). Inherited-URI
     * changes are captured at the declaring element. */
    for (struct leptris_namespace* na = leptris_elem_namespaces(a);
         na; na = na->next) {
        const char* b_uri = NULL;
        for (struct leptris_namespace* nb2 =
                 leptris_elem_namespaces(b);
             nb2; nb2 = nb2->next) {
            if ((na->prefix == NULL && nb2->prefix == NULL) ||
                (na->prefix && nb2->prefix &&
                 strcmp(na->prefix, nb2->prefix) == 0)) {
                b_uri = nb2->uri;
                break;
            }
        }
        char nm[160];
        if (na->prefix)
            snprintf(nm, sizeof nm, "xmlns:%s", na->prefix);
        else
            snprintf(nm, sizeof nm, "xmlns");
        if (!b_uri)
            d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, nm,
                   na->uri ? na->uri : "", "");
        else if (strcmp(na->uri ? na->uri : "", b_uri) != 0)
            d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, nm,
                   na->uri ? na->uri : "", b_uri);
    }
    for (struct leptris_namespace* nb2 = leptris_elem_namespaces(b);
         nb2; nb2 = nb2->next) {
        int found = 0;
        for (struct leptris_namespace* na = leptris_elem_namespaces(a);
             na; na = na->next) {
            if ((na->prefix == NULL && nb2->prefix == NULL) ||
                (na->prefix && nb2->prefix &&
                 strcmp(na->prefix, nb2->prefix) == 0)) {
                found = 1;
                break;
            }
        }
        if (found) continue;
        char nm[160];
        if (nb2->prefix)
            snprintf(nm, sizeof nm, "xmlns:%s", nb2->prefix);
        else
            snprintf(nm, sizeof nm, "xmlns");
        d_push(d, LEPTRIS_DIFF_UPDATE_ATTR, path, nm, "",
               nb2->uri ? nb2->uri : "");
    }

    /* Children: LCS over digests, then run alignment. */
    LeptrisNodeRef A[DIFF_MAX_CHILDREN], B[DIFF_MAX_CHILDREN];
    size_t na = 0, nb = 0;
    for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)a);
         c && na < DIFF_MAX_CHILDREN;
         c = leptris_node_get_next_sibling(c)) {
        if ((flags & LEPTRIS_DIFF_IGNORE_WS_TEXT) &&
            leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_TEXT &&
            d_ws_only(leptris_text_node_get_content(c)))
            continue;
        A[na++] = c;
    }
    for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)b);
         c && nb < DIFF_MAX_CHILDREN;
         c = leptris_node_get_next_sibling(c)) {
        if ((flags & LEPTRIS_DIFF_IGNORE_WS_TEXT) &&
            leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_TEXT &&
            d_ws_only(leptris_text_node_get_content(c)))
            continue;
        B[nb++] = c;
    }
    if (!na && !nb) return;

    uint64_t* da = (uint64_t*)malloc((na + 1) * sizeof(uint64_t));
    uint64_t* db = (uint64_t*)malloc((nb + 1) * sizeof(uint64_t));
    size_t* dp = (size_t*)malloc((na + 1) * (nb + 1) * sizeof(size_t));
    if (!da || !db || !dp) {
        free(da);
        free(db);
        free(dp);
        return;
    }
    for (size_t i = 0; i < na; i++)
        da[i] = leptris_node_digest(A[i], df);
    for (size_t j = 0; j < nb; j++)
        db[j] = leptris_node_digest(B[j], df);
    /* LCS table */
    for (size_t j = 0; j <= nb; j++) dp[0 * (nb + 1) + j] = 0;
    for (size_t i = 1; i <= na; i++) {
        dp[i * (nb + 1) + 0] = 0;
        for (size_t j = 1; j <= nb; j++) {
            size_t up = dp[(i - 1) * (nb + 1) + j];
            size_t left = dp[i * (nb + 1) + (j - 1)];
            if (da[i - 1] == db[j - 1]) {
                dp[i * (nb + 1) + j] =
                    dp[(i - 1) * (nb + 1) + (j - 1)] + 1;
            } else {
                dp[i * (nb + 1) + j] = up > left ? up : left;
            }
        }
    }
    /* Backtrack into runs: collect (A-run, B-run) segments. */
    size_t i = na, j = nb;
    LeptrisNodeRef runA[DIFF_MAX_CHILDREN];
    LeptrisNodeRef runB[DIFF_MAX_CHILDREN];
    while (i > 0 || j > 0) {
        size_t ra = 0, rb = 0;
        while ((i > 0 && j > 0 && da[i - 1] != db[j - 1]) ||
               (i > 0 && j == 0) || (j > 0 && i == 0)) {
            if (i > 0 &&
                (j == 0 ||
                 dp[(i - 1) * (nb + 1) + j] >=
                     dp[i * (nb + 1) + (j - 1)])) {
                runA[ra++] = A[--i];
            } else {
                runB[rb++] = B[--j];
            }
        }
        if (i > 0 && j > 0 && da[i - 1] == db[j - 1]) {
            i--;
            j--;
        }
        /* Align the collected run (reverse to document order). */
        int usedA[DIFF_MAX_CHILDREN], usedB[DIFF_MAX_CHILDREN];
        for (size_t k = 0; k < ra; k++) usedA[k] = 0;
        for (size_t k = 0; k < rb; k++) usedB[k] = 0;
        /* Pair same-named elements (greedy, in order). */
        for (size_t x = ra; x > 0; x--) {
            if (usedA[x - 1]) continue;
            LeptrisNodeRef an = runA[x - 1];
            if (leptris_node_get_type(an) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            const char* anm = leptris_element_name((LeptrisElement)an);
            for (size_t y = rb; y > 0; y--) {
                if (usedB[y - 1]) continue;
                LeptrisNodeRef bn = runB[y - 1];
                if (leptris_node_get_type(bn) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                if (strcmp(leptris_element_name((LeptrisElement)bn),
                           anm) == 0) {
                    char cp[512];
                    d_child_path(cp, sizeof cp, path, bn, 0);
                    d_diff_element(d, (LeptrisElement)an,
                                   (LeptrisElement)bn, cp, flags);
                    usedA[x - 1] = usedB[y - 1] = 1;
                    break;
                }
            }
        }
        /* Pair text nodes positionally. */
        size_t tb = 0;
        for (size_t y = rb; y > 0; y--) {
            if (usedB[y - 1]) continue;
            if (leptris_node_get_type(runB[y - 1]) !=
                LEPTRIS_NODE_TYPE_TEXT)
                continue;
            const char* btxt = leptris_text_node_get_content(
                runB[y - 1]);
            const char* atxt = NULL;
            for (size_t x = ra; x > 0; x--) {
                if (usedA[x - 1]) continue;
                if (leptris_node_get_type(runA[x - 1]) !=
                    LEPTRIS_NODE_TYPE_TEXT)
                    continue;
                atxt = leptris_text_node_get_content(runA[x - 1]);
                usedA[x - 1] = 1;
                break;
            }
            usedB[y - 1] = 1;
            if (!atxt) atxt = "";
            if (strcmp(atxt, btxt ? btxt : "") != 0 || tb)
                d_push(d, LEPTRIS_DIFF_UPDATE_TEXT, path, "", atxt,
                       btxt ? btxt : "");
        }
        for (size_t x = ra; x > 0; x--) {
            if (usedA[x - 1]) continue;
            if (leptris_node_get_type(runA[x - 1]) ==
                LEPTRIS_NODE_TYPE_TEXT) {
                const char* atxt =
                    leptris_text_node_get_content(runA[x - 1]);
                int paired = 0;
                for (size_t y = rb; y > 0; y--) {
                    if (usedB[y - 1]) continue;
                    if (leptris_node_get_type(runB[y - 1]) ==
                        LEPTRIS_NODE_TYPE_TEXT) {
                        usedB[y - 1] = 1;
                        paired = 1;
                        break;
                    }
                }
                if (!paired)
                    d_push(d, LEPTRIS_DIFF_UPDATE_TEXT, path, "",
                           atxt, "");
            }
        }
        /* Remaining unpaired: DELETE (A) then INSERT (B). */
        for (size_t x = ra; x > 0; x--) {
            if (usedA[x - 1]) continue;
            char cp[512];
            d_child_path(cp, sizeof cp, path, runA[x - 1], 1);
            const char* nm =
                leptris_node_get_type(runA[x - 1]) ==
                        LEPTRIS_NODE_TYPE_ELEMENT
                    ? leptris_element_name(
                          (LeptrisElement)runA[x - 1])
                    : "#node";
            d_push(d, LEPTRIS_DIFF_DELETE, cp, nm, "", "");
        }
        for (size_t y = rb; y > 0; y--) {
            if (usedB[y - 1]) continue;
            char cp[512];
            d_child_path(cp, sizeof cp, path, runB[y - 1], 1);
            const char* nm =
                leptris_node_get_type(runB[y - 1]) ==
                        LEPTRIS_NODE_TYPE_ELEMENT
                    ? leptris_element_name(
                          (LeptrisElement)runB[y - 1])
                    : "#node";
            d_push(d, LEPTRIS_DIFF_INSERT, cp, nm, "", "");
        }
    }
    free(da);
    free(db);
    free(dp);
}

/* ---- public entries ---- */

LEPTRIS_API LeptrisDiff leptris_diff(LeptrisDocument a,
                                     LeptrisDocument b,
                                     LeptrisDiffFlags flags,
                                     LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!a || !b) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    struct leptris_diff* d =
        (struct leptris_diff*)calloc(1, sizeof(*d));
    if (!d) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    LeptrisElement ra = leptris_document_root(a);
    LeptrisElement rb = leptris_document_root(b);
    if (ra && rb) {
        if (strcmp(leptris_element_name(ra),
                   leptris_element_name(rb)) == 0) {
            char rootp[256];
            snprintf(rootp, sizeof rootp, "/%s",
                     leptris_element_name(ra));
            d_diff_element(d, ra, rb, rootp, flags);
        } else {
            d_push(d, LEPTRIS_DIFF_DELETE, "/",
                   leptris_element_name(ra), "", "");
            d_push(d, LEPTRIS_DIFF_INSERT, "/",
                   leptris_element_name(rb), "", "");
        }
    } else if (ra) {
        d_push(d, LEPTRIS_DIFF_DELETE, "/",
               leptris_element_name(ra), "", "");
    } else if (rb) {
        d_push(d, LEPTRIS_DIFF_INSERT, "/",
               leptris_element_name(rb), "", "");
    }
    return d;
}

LEPTRIS_API void leptris_diff_free(LeptrisDiff diff) {
    struct leptris_diff* d = (struct leptris_diff*)diff;
    if (!d) return;
    for (size_t i = 0; i < d->n; i++) {
        free(d->ops[i].path);
        free(d->ops[i].name);
        free(d->ops[i].before);
        free(d->ops[i].after);
    }
    free(d->ops);
    free(d);
}

LEPTRIS_API size_t leptris_diff_op_count(LeptrisDiff diff) {
    return diff ? diff->n : 0;
}

LEPTRIS_API LeptrisDiffOpType leptris_diff_op_type(LeptrisDiff diff,
                                                   size_t index) {
    if (!diff || index >= diff->n) return (LeptrisDiffOpType)0;
    return (LeptrisDiffOpType)diff->ops[index].type;
}

LEPTRIS_API const char* leptris_diff_op_path(LeptrisDiff diff,
                                             size_t index) {
    if (!diff || index >= diff->n) return NULL;
    return diff->ops[index].path;
}

LEPTRIS_API const char* leptris_diff_op_name(LeptrisDiff diff,
                                             size_t index) {
    if (!diff || index >= diff->n) return NULL;
    return diff->ops[index].name;
}

LEPTRIS_API const char* leptris_diff_op_before(LeptrisDiff diff,
                                               size_t index) {
    if (!diff || index >= diff->n) return NULL;
    return diff->ops[index].before;
}

LEPTRIS_API const char* leptris_diff_op_after(LeptrisDiff diff,
                                              size_t index) {
    if (!diff || index >= diff->n) return NULL;
    return diff->ops[index].after;
}

LEPTRIS_API char* leptris_diff_serialize(LeptrisDiff diff) {
    if (!diff) return leptris_strdup("");
    size_t cap = 64;
    for (size_t i = 0; i < diff->n; i++)
        cap += strlen(diff->ops[i].path) +
               strlen(diff->ops[i].name) +
               strlen(diff->ops[i].before) +
               strlen(diff->ops[i].after) + 32;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    size_t len = 0;
    for (size_t i = 0; i < diff->n; i++) {
        DiffOp* o = &diff->ops[i];
        if (o->type == LEPTRIS_DIFF_UPDATE_ATTR)
            len += (size_t)snprintf(
                out + len, cap - len, "- %s @%s \"%s\" -> \"%s\"\n",
                o->path, o->name, o->before, o->after);
        else if (o->type == LEPTRIS_DIFF_UPDATE_TEXT)
            len += (size_t)snprintf(
                out + len, cap - len, "~ %s \"%s\" -> \"%s\"\n",
                o->path, o->before, o->after);
        else if (o->type == LEPTRIS_DIFF_INSERT)
            len += (size_t)snprintf(out + len, cap - len,
                                    "+ %s <%s>\n", o->path, o->name);
        else
            len += (size_t)snprintf(out + len, cap - len,
                                    "x %s <%s>\n", o->path, o->name);
    }
    return out;
}
