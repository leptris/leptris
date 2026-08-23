/* xpath_ast_cache.c — LRU cache of parsed XPath expression ASTs.
 *
 * Per TODO 113 perf audit: parsing the expression accounts for a
 * large fraction of leptris_xpath_eval time. For workloads that
 * evaluate the same expression repeatedly (very common), caching
 * the parsed AST eliminates the parse cost on every call after
 * the first.
 *
 * TODO 120 Phase F: the cache now also holds the compiled bytecode
 * alongside the AST. The bytecode is compiled lazily on first VM
 * eval and reused thereafter, so repeated evals skip both the
 * parse and the compile phase.
 *
 * Implementation: fixed 16-slot open-addressed hash by expression
 * string hash, guarded by a process-global mutex
 * (TODO.concurrency/08).
 *
 * Tradeoff: this leaks ASTs and bytecodes at process exit.
 * Acceptable for a process-global cache of small (~100 byte) ASTs
 * and ~1 KB bytecodes. */

#include "xpath_internal.h"
#include "../common/port.h"  /* LEPTRIS_MUTEX */
#include "parser.h"  /* ast_node_free */
#include "bytecode.h"  /* leptris_xpath_bytecode_free */
#include "../leptris_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define XPATH_AST_CACHE_SLOTS 16

typedef struct {
    unsigned hash;        /* FNV-1a hash; 0 = empty slot */
    char* expr_copy;      /* Owned string copy (for re-lookup verification) */
    size_t expr_len;
    XPathASTNode* ast;    /* Owned AST */
    LeptrisXPathBytecode* bc;  /* Owned bytecode (lazy; NULL until first VM eval) */
    int pins;             /* In-flight borrows (TODO.concurrency/08) */
} xpath_ast_cache_slot;

static xpath_ast_cache_slot g_cache[XPATH_AST_CACHE_SLOTS];

/* TODO.concurrency/08: an evicted entry with pins > 0 moves here
 * until its last borrower releases. AST/bytecode pointers escape
 * the mutex for the duration of one evaluate — eviction may not
 * free them underneath a running thread. */
typedef struct cache_graveyard {
    char* expr_copy;
    XPathASTNode* ast;
    LeptrisXPathBytecode* bc;
    int pins;
    struct cache_graveyard* next;
} cache_graveyard;

static cache_graveyard* g_graveyard;  /* guarded by g_cache_mutex */

/* TODO.concurrency/08: the process-wide cache is mutex-guarded, not
 * thread-local — a per-thread cache would leak its ASTs on thread
 * exit (no portable TLS destructors in C99), and bindings evaluate
 * from pooled threads. Uncontended lock/unlock is ~20 ns; the
 * parse-once reuse win is preserved across threads. */
static leptris_mutex_t g_cache_mutex = LEPTRIS_MUTEX_INIT;

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

static XPathASTNode* cache_lookup_locked(const char* expr, size_t expr_len) {
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

/* Combined lookup (TODO 159 Phase E drive-by): single hash + scan
 * that returns both AST and bytecode. Replaces the previous pattern
 * of calling xpath_ast_cache_lookup + xpath_ast_cache_get_bc which
 * hashed the expression twice per leptris_xpath_eval call. */
static int cache_get_locked(const char* expr, size_t expr_len,
                             XPathCacheEntry* out) {
    if (!expr || expr_len == 0 || !out) return 0;
    out->ast = NULL;
    out->bc = NULL;
    unsigned h = xpath_hash(expr, expr_len);
    for (size_t probe = 0; probe < XPATH_AST_CACHE_SLOTS; probe++) {
        size_t i = (h + probe) % XPATH_AST_CACHE_SLOTS;
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->hash == 0) return 0;  /* empty */
        if (slot->hash == h &&
            slot->expr_len == expr_len &&
            memcmp(slot->expr_copy, expr, expr_len) == 0) {
            out->ast = slot->ast;
            out->bc = slot->bc;
            slot->pins++;  /* borrow pinned for one evaluate */
            return 1;
        }
    }
    return 0;
}

/* Returns the canonical AST for `expr` after insert. The caller
 * MUST use the returned pointer (a racing twin insert may have won
 * the slot; the passed AST is then freed and using it would be a
 * use-after-free). The returned entry comes back pinned for the
 * caller — pair with xpath_ast_cache_release. */
static XPathASTNode* cache_insert_locked(const char* expr, size_t expr_len, XPathASTNode* ast) {
    if (!expr || expr_len == 0 || !ast) return ast;
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
            /* Already cached (racing twin won). The cached one is
             * canonical; free the caller's private twin. */
            ast_node_free(ast);
            slot->pins++;
            return slot->ast;
        }
    }

    /* Overwrite target slot. A pinned entry moves to the graveyard
     * instead of being freed underneath its borrowers. */
    xpath_ast_cache_slot* slot = &g_cache[target];
    if (slot->expr_copy) {
        if (slot->pins > 0) {
            cache_graveyard* g =
                (cache_graveyard*)malloc(sizeof(*g));
            if (g) {
                g->expr_copy = slot->expr_copy;
                g->ast = slot->ast;
                g->bc = slot->bc;
                g->pins = slot->pins;
                g->next = g_graveyard;
                g_graveyard = g;
            }
        } else {
            LEPTRIS_FREE(slot->expr_copy);
            ast_node_free(slot->ast);
            if (slot->bc) leptris_xpath_bytecode_free(slot->bc);
        }
    }
    /* Copy the expression string so future lookups can verify. */
    char* copy = LEPTRIS_ALLOC_N(char, expr_len + 1);
    if (!copy) return NULL;
    memcpy(copy, expr, expr_len);
    copy[expr_len] = '\0';

    slot->hash = h;
    slot->expr_copy = copy;
    slot->expr_len = expr_len;
    slot->ast = ast;
    slot->bc = NULL;  /* lazy; compiled on first VM eval */
    slot->pins = 1;   /* pinned for the inserting caller */
    return ast;
}

static void cache_release_locked(XPathASTNode* ast) {
    if (!ast) return;
    for (size_t i = 0; i < XPATH_AST_CACHE_SLOTS; i++) {
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->ast == ast) {
            if (slot->pins > 0) slot->pins--;
            return;
        }
    }
    for (cache_graveyard** pp = &g_graveyard; *pp; pp = &(*pp)->next) {
        cache_graveyard* g = *pp;
        if (g->ast == ast) {
            if (--g->pins <= 0) {
                *pp = g->next;
                LEPTRIS_FREE(g->expr_copy);
                ast_node_free(g->ast);
                if (g->bc) leptris_xpath_bytecode_free(g->bc);
                free(g);
            }
            return;
        }
    }
}

static LeptrisXPathBytecode* cache_get_bc_locked(const char* expr, size_t expr_len) {
    if (!expr || expr_len == 0) return NULL;
    unsigned h = xpath_hash(expr, expr_len);
    for (size_t probe = 0; probe < XPATH_AST_CACHE_SLOTS; probe++) {
        size_t i = (h + probe) % XPATH_AST_CACHE_SLOTS;
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->hash == 0) return NULL;  /* empty */
        if (slot->hash == h &&
            slot->expr_len == expr_len &&
            memcmp(slot->expr_copy, expr, expr_len) == 0) {
            return slot->bc;
        }
    }
    return NULL;
}

static void cache_store_bc_locked(const char* expr, size_t expr_len,
                                   LeptrisXPathBytecode* bc) {
    if (!expr || expr_len == 0 || !bc) {
        if (bc) leptris_xpath_bytecode_free(bc);
        return;
    }
    unsigned h = xpath_hash(expr, expr_len);
    for (size_t probe = 0; probe < XPATH_AST_CACHE_SLOTS; probe++) {
        size_t i = (h + probe) % XPATH_AST_CACHE_SLOTS;
        xpath_ast_cache_slot* slot = &g_cache[i];
        if (slot->hash == 0) {
            /* Expression not in the cache. Free the orphan bytecode;
             * the caller's path (leptris_xpath_eval) should always
             * insert the AST before storing the bc, so this branch
             * is defensive. */
            leptris_xpath_bytecode_free(bc);
            return;
        }
        if (slot->hash == h &&
            slot->expr_len == expr_len &&
            memcmp(slot->expr_copy, expr, expr_len) == 0) {
            /* Race-safe last-writer-wins. If two threads compile
             * concurrently, the loser frees its bytecode to avoid
             * a leak. The winner's bytecode stays referenced until
             * the slot is overwritten. */
            if (slot->bc) leptris_xpath_bytecode_free(bc);
            else slot->bc = bc;
            return;
        }
    }
    /* Not found — defensive free. */
    leptris_xpath_bytecode_free(bc);
}

/* ---- Public entry points: mutex-guarded (TODO.concurrency/08) ---- */

XPathASTNode* xpath_ast_cache_lookup(const char* expr, size_t expr_len) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    XPathASTNode* r = cache_lookup_locked(expr, expr_len);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
    return r;
}

int xpath_ast_cache_get(const char* expr, size_t expr_len,
                         XPathCacheEntry* out) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    int r = cache_get_locked(expr, expr_len, out);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
    return r;
}

XPathASTNode* xpath_ast_cache_insert(const char* expr, size_t expr_len, XPathASTNode* ast) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    XPathASTNode* r = cache_insert_locked(expr, expr_len, ast);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
    return r;
}

void xpath_ast_cache_release(XPathASTNode* ast) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    cache_release_locked(ast);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
}

LeptrisXPathBytecode* xpath_ast_cache_get_bc(const char* expr, size_t expr_len) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    LeptrisXPathBytecode* r = cache_get_bc_locked(expr, expr_len);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
    return r;
}

void xpath_ast_cache_store_bc(const char* expr, size_t expr_len,
                               LeptrisXPathBytecode* bc) {
    LEPTRIS_MUTEX_LOCK(&g_cache_mutex);
    cache_store_bc_locked(expr, expr_len, bc);
    LEPTRIS_MUTEX_UNLOCK(&g_cache_mutex);
}
