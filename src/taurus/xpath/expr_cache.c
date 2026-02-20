/* expr_cache.c - XPath Expression Cache Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * PERFORMANCE: Cache parsed XPath AST to avoid repeated parsing overhead.
 */

#include "expr_cache.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>

/* FNV-1a hash constants for 32-bit */
#define FNV_OFFSET 0x811c9dc5
#define FNV_PRIME  0x01000193

/* Default cache size */
#define DEFAULT_CACHE_SIZE 32

/**
 * Compute FNV-1a hash of string
 */
static uint32_t fnv1a_hash(const char* str, size_t len) {
    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/**
 * Find slot for expression
 *
 * @return Slot index or capacity if not found and no empty slot
 */
static size_t find_slot(XPathExprCache* cache, const char* expr_str, size_t len, uint32_t hash) {
    size_t idx = hash & (cache->capacity - 1);
    size_t start = idx;

    do {
        CachedExpr* entry = &cache->entries[idx];

        /* Empty slot */
        if (entry->expr_str == NULL) {
            return idx;
        }

        /* Check for match */
        if (entry->hash == hash) {
            /* Hash match - verify string */
            if (strncmp(entry->expr_str, expr_str, len) == 0 &&
                entry->expr_str[len] == '\0') {
                return idx;  /* Found */
            }
        }

        /* Linear probe */
        idx = (idx + 1) & (cache->capacity - 1);
    } while (idx != start);

    return cache->capacity;  /* Cache full */
}

XPathExprCache* xpath_expr_cache_create(size_t capacity) {
    /* Use default size if not specified or invalid */
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        capacity = DEFAULT_CACHE_SIZE;
    }

    XPathExprCache* cache = malloc(sizeof(XPathExprCache));
    if (!cache) return NULL;

    cache->entries = calloc(capacity, sizeof(CachedExpr));
    if (!cache->entries) {
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->count = 0;
    cache->access_counter = 0;
    cache->hits = 0;
    cache->misses = 0;

    return cache;
}

void xpath_expr_cache_free(XPathExprCache* cache) {
    if (!cache) return;

    /* Free all entries */
    for (size_t i = 0; i < cache->capacity; i++) {
        if (cache->entries[i].expr_str) {
            free(cache->entries[i].expr_str);
            if (cache->entries[i].ast) {
                ast_node_free(cache->entries[i].ast);
            }
        }
    }

    free(cache->entries);
    free(cache);
}

struct XPathASTNode* xpath_expr_cache_lookup(
    XPathExprCache* cache,
    const char* expr_str,
    size_t len
) {
    if (!cache || !expr_str || len == 0) return NULL;

    uint32_t hash = fnv1a_hash(expr_str, len);
    size_t idx = find_slot(cache, expr_str, len, hash);

    if (idx < cache->capacity && cache->entries[idx].expr_str != NULL) {
        /* Cache hit */
        cache->entries[idx].access_count++;
        cache->entries[idx].last_access = ++cache->access_counter;
        cache->hits++;
        return cache->entries[idx].ast;
    }

    /* Cache miss */
    cache->misses++;
    return NULL;
}

int xpath_expr_cache_store(
    XPathExprCache* cache,
    const char* expr_str,
    size_t len,
    struct XPathASTNode* ast
) {
    if (!cache || !expr_str || len == 0 || !ast) return -1;

    uint32_t hash = fnv1a_hash(expr_str, len);
    size_t idx = find_slot(cache, expr_str, len, hash);

    /* Check if already exists */
    if (idx < cache->capacity && cache->entries[idx].expr_str != NULL) {
        /* Already cached - this shouldn't happen normally */
        return 0;
    }

    /* Cache full - evict LRU entry */
    if (cache->count >= cache->capacity * 3 / 4) {
        /* Find LRU entry */
        size_t lru_idx = 0;
        uint64_t lru_time = UINT64_MAX;

        for (size_t i = 0; i < cache->capacity; i++) {
            if (cache->entries[i].expr_str != NULL &&
                cache->entries[i].last_access < lru_time) {
                lru_time = cache->entries[i].last_access;
                lru_idx = i;
            }
        }

        /* Free LRU entry */
        free(cache->entries[lru_idx].expr_str);
        if (cache->entries[lru_idx].ast) {
            ast_node_free(cache->entries[lru_idx].ast);
        }
        cache->entries[lru_idx].expr_str = NULL;
        cache->entries[lru_idx].ast = NULL;
        cache->count--;

        /* Find new slot */
        idx = find_slot(cache, expr_str, len, hash);
    }

    if (idx >= cache->capacity) {
        return -1;  /* No space */
    }

    /* Copy expression string */
    char* expr_copy = malloc(len + 1);
    if (!expr_copy) return -1;
    memcpy(expr_copy, expr_str, len);
    expr_copy[len] = '\0';

    /* Store entry */
    cache->entries[idx].expr_str = expr_copy;
    cache->entries[idx].ast = ast;
    cache->entries[idx].hash = hash;
    cache->entries[idx].access_count = 1;
    cache->entries[idx].last_access = ++cache->access_counter;
    cache->count++;

    return 0;
}

void xpath_expr_cache_stats(
    XPathExprCache* cache,
    size_t* hits,
    size_t* misses
) {
    if (!cache) {
        if (hits) *hits = 0;
        if (misses) *misses = 0;
        return;
    }
    if (hits) *hits = cache->hits;
    if (misses) *misses = cache->misses;
}

void xpath_expr_cache_clear(XPathExprCache* cache) {
    if (!cache) return;

    for (size_t i = 0; i < cache->capacity; i++) {
        if (cache->entries[i].expr_str) {
            free(cache->entries[i].expr_str);
            if (cache->entries[i].ast) {
                ast_node_free(cache->entries[i].ast);
            }
            cache->entries[i].expr_str = NULL;
            cache->entries[i].ast = NULL;
        }
    }

    cache->count = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->access_counter = 0;
}
