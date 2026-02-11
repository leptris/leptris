/* parse_helpers.c - Implementation of parse helper functions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 */

#include "parse_helpers.h"
#include <string.h>

/* ==================================================================
 * STRING INTERNING IMPLEMENTATION
 * ================================================================= */

/* Pre-interned strings for common attributes - defined as string literals */
const char *g_intern_id = "id";
const char *g_intern_class = "class";
const char *g_intern_type = "type";
const char *g_intern_name = "name";
const char *g_intern_href = "href";
const char *g_intern_src = "src";
const char *g_intern_xmlns = "xmlns";
const char *g_intern_style = "style";
const char *g_intern_value = "value";
const char *g_intern_lang = "lang";

/* Simple hash function - DJB2 */
static size_t hash_string(const char *str, size_t len) {
    size_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    }
    return hash;
}

/* Initialize string interning table */
void string_intern_table_init(StringInternTable *table) {
    memset(table->entries, 0, sizeof(table->entries));
    table->initialized = 1;
}

/* Fast-path for common attributes (bypasses hash table)
 * Returns interned string directly for top 10 attributes, otherwise NULL */
const char* string_intern_get_fast(const char *name, size_t len) {
    /* Quick length-based dispatch */
    switch (len) {
        case 2:
            if (name[0] == 'i' && name[1] == 'd') return g_intern_id;
            break;
        case 3:
            if (name[0] == 's' && name[1] == 'r' && name[2] == 'c') 
                return g_intern_src;
            break;
        case 4:
            if (memcmp(name, "type", 4) == 0) return g_intern_type;
            if (memcmp(name, "name", 4) == 0) return g_intern_name;
            if (memcmp(name, "href", 4) == 0) return g_intern_href;
            if (memcmp(name, "lang", 4) == 0) return g_intern_lang;
            break;
        case 5:
            if (memcmp(name, "class", 5) == 0) return g_intern_class;
            if (memcmp(name, "style", 5) == 0) return g_intern_style;
            if (memcmp(name, "value", 5) == 0) return g_intern_value;
            if (memcmp(name, "xmlns", 5) == 0) return g_intern_xmlns;
            break;
    }
    return NULL;
}

/* Get or create interned string
 * Uses linear probing hash table for collision resolution */
const char* string_intern_get(StringInternTable *table, const char *str, size_t len) {
    /* Try fast path first (common attributes) */
    const char *fast = string_intern_get_fast(str, len);
    if (fast != NULL) {
        return fast;
    }

    /* Compute hash */
    size_t hash = hash_string(str, len);
    size_t index = hash & (STRING_INTERN_TABLE_SIZE - 1);  /* Modulo by power of 2 */

    /* Linear probing */
    for (size_t i = 0; i < STRING_INTERN_TABLE_SIZE; i++) {
        size_t probe = (index + i) & (STRING_INTERN_TABLE_SIZE - 1);
        StringInternEntry *entry = &table->entries[probe];

        /* Empty slot - insert new entry */
        if (entry->key == NULL) {
            /* Allocate and copy string */
            char *copy = (char*)taurus_malloc(len + 1);
            memcpy(copy, str, len);
            copy[len] = '\0';

            entry->key = copy;
            entry->hash = hash;
            return copy;
        }

        /* Existing entry - check if it matches */
        if (entry->hash == hash && strlen(entry->key) == len) {
            if (memcmp(entry->key, str, len) == 0) {
                return entry->key;  /* Found existing interned string */
            }
        }
    }

    /* Table is full - fall back to allocating new string
     * (This should be rare with 128 slots) */
    char *copy = (char*)taurus_malloc(len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

/* Clear the interning table (for testing/benchmarking) */
void string_intern_table_clear(StringInternTable *table) {
    for (size_t i = 0; i < STRING_INTERN_TABLE_SIZE; i++) {
        if (table->entries[i].key != NULL) {
            /* Don't free pre-interned strings (they're string literals) */
            if (table->entries[i].key != g_intern_id &&
                table->entries[i].key != g_intern_class &&
                table->entries[i].key != g_intern_type &&
                table->entries[i].key != g_intern_name &&
                table->entries[i].key != g_intern_href &&
                table->entries[i].key != g_intern_src &&
                table->entries[i].key != g_intern_xmlns &&
                table->entries[i].key != g_intern_style &&
                table->entries[i].key != g_intern_value &&
                table->entries[i].key != g_intern_lang) {
                taurus_free((void*)table->entries[i].key);
            }
            table->entries[i].key = NULL;
            table->entries[i].hash = 0;
        }
    }
}

/* Free all interned strings */
void string_intern_table_free(StringInternTable *table) {
    string_intern_table_clear(table);
    table->initialized = 0;
}