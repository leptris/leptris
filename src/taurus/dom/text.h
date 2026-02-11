/* lib/src/dom/text.h - Text node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Text nodes contain character data.
 * CRITICAL: Content is NEVER trimmed - preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_TEXT_H
#define TAURUS_DOM_TEXT_H

#include "node.h"

/* Text node - inherits from TaurusNode */
typedef struct taurus_text_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Text content - NEVER trim! */
    void* next_sibling;               /* Next sibling in linked list (mixed content) */
} TaurusTextNode;

/* Text node creation and destruction */
TaurusTextNode* taurus_text_create(const char* content);

/* Create text node with bulk allocation (optimized) */
TaurusTextNode* taurus_text_create_fast(
    const char* content,
    size_t content_len,
    TaurusMemoryPool* pool
);

void taurus_text_free(TaurusTextNode* text);

/* Content access */
const char* taurus_text_get_content(TaurusTextNode* text);
void taurus_text_set_content(TaurusTextNode* text, const char* content);

/* Casting helpers */
#define TAURUS_NODE_AS_TEXT(node) \
    (TAURUS_NODE_IS_TEXT(node) ? (TaurusTextNode*)(node) : NULL)

#define TAURUS_TEXT_AS_NODE(text) \
    ((TaurusNode*)(text))

#endif /* TAURUS_DOM_TEXT_H */