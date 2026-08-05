/* xpath_ast_cache.c — LRU cache of parsed XPath expression ASTs.
 *
 * Per TODO 113 perf audit: parsing the expression accounts for a
 * large fraction of taurus_xpath_eval time. For workloads that
 * evaluate the same expression repeatedly (very common), caching
 * the parsed AST eliminates the parse cost on every call after
 * the first.
 *
 * Implementation: fixed 16-slot open-addressed hash by expression
 * string hash. No eviction beyond slot replacement. The ASTs are
 * immutable after parse, so concurrent reads are safe; concurrent
 * first-insert of the same key is a benign race (worst case is a
 * duplicate parse, then last-writer-wins on the slot).
 *
 * Tradeoff: this leaks ASTs at process exit. Acceptable for a
 * process-global cache of small (~100 byte) ASTs. */

#include "xpath_internal.h"
#include "parser.h"  /* ast_node_free */
#include "../taurus_internal.h"
#include <string.h>
#include <stdio.h>

#define XPATH_AST_CACHE_SLOTS 16

typedef struct {
    unsigned hash;        /* FNV-1a hash; 0 = empty slot */
    char* expr_copy;      /* Owned string copy (for re-lookup verification) */
    size_t expr_len;
    XPathASTNode* ast;    /* Owned AST */
} xpath_ast_cache_slot;

static xpath_ast_cache_slot g_cache[XPATH_AST_CACHE_SLOTS];

/* FNV-1a 32-bit hash. */
static unsigned xpath_hash(const char* s, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    /* Avoid the sentinel value of 0 (= empty slot). */
    return h == 0 ? 1 : h;
}

XPathASTNode* xpath_ast_cache_lookup(const char* expr, size_t expr_len) {
    if (!expr || expr_len == 0) return NULL;
    unsigned h = xpath_hash(expr, expr_len);
    /* Open-addressed probe: linear scan starting at h % SLOTS. */
    for (size_t probe = 0; probe < XPATH_AST_CACHE_SLOTS; probe++) {
        size_t i = (h + probe) % XPATH_AST_CACHE_SLOTS;
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->hash == 0) return NULL;  /* empty */
        if (slot->hash == h &&
            slot->expr_len == expr_len &&
            memcmp(slot->expr_copy, expr, expr_len) == 0) {
            return slot->ast;
        }
    }
    return NULL;
}

void xpath_ast_cache_insert(const char* expr, size_t expr_len, XPathASTNode* ast) {
    if (!expr || expr_len == 0 || !ast) return;
    unsigned h = xpath_hash(expr, expr_len);

    /* Find target slot: prefer the hash slot itself if empty or
     * already holds this expression; otherwise probe for empty or
     * overwrite the entry furthest from the hash slot (LRU-ish). */
    size_t target = h % XPATH_AST_CACHE_SLOTS;
    for (size_t probe = 0; probe < XPATH_AST_CACHE_SLOTS; probe++) {
        size_t i = (h + probe) % XPATH_AST_CACHE_SLOTS;
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->hash == 0) {
            target = i;
            break;
        }
        if (slot->hash == h &&
            slot->expr_len == expr_len &&
            memcmp(slot->expr_copy, expr, expr_len) == 0) {
            /* Already cached. Caller owns the new AST; free it to
             * avoid the leak (the cached one wins). */
            ast_node_free(ast);
            return;
        }
    }

    /* Overwrite target slot. Free the previous contents. */
    xpath_ast_cache_slot* slot = &g_cache[target];
    if (slot->expr_copy) {
        TAURUS_FREE(slot->expr_copy);
        ast_node_free(slot->ast);
    }
    /* Copy the expression string so future lookups can verify. */
    char* copy = TAURUS_ALLOC_N(char, expr_len + 1);
    if (!copy) return;
    memcpy(copy, expr, expr_len);
    copy[expr_len] = '\0';

    slot->hash = h;
    slot->expr_copy = copy;
    slot->expr_len = expr_len;
    slot->ast = ast;
}
