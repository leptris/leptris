/* xpath_internal.h - Internal XPath structures
 * Copyright (c) 2024, Ribose Inc.
 * INTERNAL HEADER - Not part of public API
 */

#ifndef XPATH_INTERNAL_H
#define XPATH_INTERNAL_H

#include "../taurus_internal.h"

/* XPath token types - Complete set from ext/taurus/lexer_xpath.c */
typedef enum {
    TOK_EOF = 0,
    TOK_SLASH,
    TOK_DOUBLE_SLASH,
    TOK_AT,
    TOK_DOT,
    TOK_DOUBLE_DOT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_DOUBLE_COLON,
    TOK_NCNAME,
    TOK_QNAME,
    TOK_STRING,
    TOK_NUMBER,
    TOK_EQUALS,
    TOK_NOT_EQUALS,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_PIPE,
    TOK_AND,
    TOK_OR,
    TOK_DIV,
    TOK_MOD,
    TOK_ANCESTOR,
    TOK_ANCESTOR_OR_SELF,
    TOK_ATTRIBUTE,
    TOK_CHILD,
    TOK_DESCENDANT,
    TOK_DESCENDANT_OR_SELF,
    TOK_FOLLOWING,
    TOK_FOLLOWING_SIBLING,
    TOK_NAMESPACE,
    TOK_PARENT,
    TOK_PRECEDING,
    TOK_PRECEDING_SIBLING,
    TOK_SELF,
    TOK_COMMENT,
    TOK_TEXT,
    TOK_PROCESSING_INSTRUCTION,
    TOK_NODE,
    TOK_DOLLAR,  /* For variable references: $var */
    TOK_VARIABLE_REFERENCE  /* Variable name after $ */
} XPathTokenType;

/* Token type names for debugging */
extern const char* xpath_token_type_names[];

/* Use XPathToken and XPathLexer types from taurus_internal.h */
/* They are already defined there, no need to redefine */

/* AST cache (TODO 113 perf). Lookup/insert parsed expression ASTs
 * so repeated evaluations skip the parse phase. */
XPathASTNode* xpath_ast_cache_lookup(const char* expr, size_t expr_len);
void xpath_ast_cache_insert(const char* expr, size_t expr_len, XPathASTNode* ast);

/* Bytecode cache (TODO 120 Phase F). Look up the compiled bytecode
 * for an expression that has already been parsed and cached as an
 * AST. Returns NULL if the bytecode has not yet been compiled or if
 * the expression is not in the cache.
 *
 * Lifetime: the returned bytecode is owned by the cache and lives
 * until the cache slot is overwritten. Callers must not free it. */
struct TaurusXPathBytecode;  /* forward; full definition in bytecode.h */
struct TaurusXPathBytecode* xpath_ast_cache_get_bc(const char* expr,
                                                    size_t expr_len);

/* Store a compiled bytecode in the cache slot for the given
 * expression. The cache takes ownership of `bc` and frees it when
 * the slot is overwritten. If the expression is not in the AST
 * cache, the bytecode is freed immediately and the function is a
 * no-op. */
void xpath_ast_cache_store_bc(const char* expr, size_t expr_len,
                               struct TaurusXPathBytecode* bc);

#endif /* XPATH_INTERNAL_H */