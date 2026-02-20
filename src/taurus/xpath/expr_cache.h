/* expr_cache.h - XPath Expression Cache
 * Copyright (c) 2024, Ribose Inc.
 *
 * PERFORMANCE: Cache parsed XPath AST to avoid repeated parsing overhead.
 * Uses FNV-1a hash for O(1) lookup.
 */

#ifndef TAURUS_XPATH_EXPR_CACHE_H
#define TAURUS_XPATH_EXPR_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct XPathASTNode;

/**
 * Cached expression entry
 */
typedef struct CachedExpr {
    char* expr_str;              /* Expression string (owned) */
    struct XPathASTNode* ast;    /* Parsed AST (owned) */
    uint32_t hash;               /* FNV-1a hash of expr_str */
    uint32_t access_count;       /* For statistics */
    uint64_t last_access;        /* For LRU eviction */
} CachedExpr;

/**
 * Expression cache structure
 *
 * Uses open addressing with linear probing for cache efficiency.
 * Small fixed size to avoid complex memory management.
 */
typedef struct XPathExprCache {
    CachedExpr* entries;         /* Cache entries */
    size_t capacity;             /* Number of slots */
    size_t count;                /* Active entries */
    uint64_t access_counter;     /* Global access counter for LRU */
    size_t hits;                 /* Cache hit count */
    size_t misses;               /* Cache miss count */
} XPathExprCache;

/**
 * Create expression cache
 *
 * @param capacity Maximum number of entries (should be power of 2)
 * @return New cache or NULL on error
 */
XPathExprCache* xpath_expr_cache_create(size_t capacity);

/**
 * Free expression cache and all cached expressions
 *
 * @param cache Cache to free
 */
void xpath_expr_cache_free(XPathExprCache* cache);

/**
 * Look up expression in cache
 *
 * @param cache Expression cache
 * @param expr_str Expression string
 * @param len Length of expression string
 * @return Cached AST or NULL if not found
 */
struct XPathASTNode* xpath_expr_cache_lookup(
    XPathExprCache* cache,
    const char* expr_str,
    size_t len
);

/**
 * Store expression in cache
 *
 * @param cache Expression cache
 * @param expr_str Expression string (will be copied)
 * @param len Length of expression string
 * @param ast Parsed AST (ownership transferred to cache)
 * @return 0 on success, -1 on error
 */
int xpath_expr_cache_store(
    XPathExprCache* cache,
    const char* expr_str,
    size_t len,
    struct XPathASTNode* ast
);

/**
 * Get cache statistics
 *
 * @param cache Expression cache
 * @param hits Output: number of cache hits
 * @param misses Output: number of cache misses
 */
void xpath_expr_cache_stats(
    XPathExprCache* cache,
    size_t* hits,
    size_t* misses
);

/**
 * Clear cache (free all entries but keep structure)
 *
 * @param cache Expression cache
 */
void xpath_expr_cache_clear(XPathExprCache* cache);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_XPATH_EXPR_CACHE_H */
