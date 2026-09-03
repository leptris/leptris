/* xpath_internal.h - Internal XPath structures
 * Copyright (c) 2024, Ribose Inc.
 * INTERNAL HEADER - Not part of public API
 */

#ifndef XPATH_INTERNAL_H
#define XPATH_INTERNAL_H

#include "../leptris_internal.h"

/* XPath token types - Complete set from ext/leptris/lexer_xpath.c */
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
    TOK_COLON,   /* map constructor key/value separator (3.1) */
    TOK_QUESTION,/* postfix lookup ?key / ?integer (3.1) */
    TOK_HASH,    /* named function reference name#arity (3.0) */
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
    TOK_ASSIGN,  /* := — XPath 3.1 let bindings */
    TOK_BANG,   /* ! — XPath 3.0 simple map */
    TOK_ARROW,  /* => — XPath 3.1 arrow operator */
    TOK_CONCAT, /* || — XPath 3.0 string concatenation */
    TOK_LBRACE, /* { — XPath 3.1 switch bodies */
    TOK_RBRACE, /* } */
    TOK_NODE_BEFORE, /* << — XPath 2.0 node comparison */
    TOK_NODE_AFTER,  /* >> — XPath 2.0 node comparison */
    TOK_VARIABLE_REFERENCE  /* Variable name after $ */
} XPathTokenType;

/* Token type names for debugging */
extern const char* xpath_token_type_names[];

/* Use XPathToken and XPathLexer types from leptris_internal.h */
/* They are already defined there, no need to redefine */

/* AST cache (TODO 113 perf). Lookup/insert parsed expression ASTs
 * so repeated evaluations skip the parse phase. */
XPathASTNode* xpath_ast_cache_lookup(const char* expr, size_t expr_len);

/* Insert returns the CANONICAL AST for the expression — use the
 * returned pointer, not the one passed in (a racing insert may have
 * won the slot and freed the caller's twin). The entry comes back
 * pinned for the caller; release it with
 * xpath_ast_cache_release(ast) after the evaluate finishes. */
XPathASTNode* xpath_ast_cache_insert(const char* expr, size_t expr_len, XPathASTNode* ast);
void xpath_ast_cache_release(XPathASTNode* ast);

/* Combined lookup (TODO 159 Phase E drive-by): single hash + scan
 * that returns both the AST and the bytecode in one pass. Replaces
 * the previous two-call pattern (xpath_ast_cache_lookup +
 * xpath_ast_cache_get_bc) which hashed the expression twice.
 *
 * Returns 1 if the expression is cached (out->ast is non-NULL);
 * returns 0 otherwise. out->bc may still be NULL on a cache hit if
 * the bytecode has not been compiled yet. */
typedef struct {
    XPathASTNode* ast;
    struct LeptrisXPathBytecode* bc;
} XPathCacheEntry;
int xpath_ast_cache_get(const char* expr, size_t expr_len,
                         XPathCacheEntry* out);

/* Bytecode cache (TODO 120 Phase F). Look up the compiled bytecode
 * for an expression that has already been parsed and cached as an
 * AST. Returns NULL if the bytecode has not yet been compiled or if
 * the expression is not in the cache.
 *
 * Lifetime: the returned bytecode is owned by the cache. Entries
 * borrowed via xpath_ast_cache_get / _insert are pinned until
 * xpath_ast_cache_release — eviction cannot free them mid-evaluate.
 * Callers must not free them. */
struct LeptrisXPathBytecode;  /* forward; full definition in bytecode.h */
struct LeptrisXPathBytecode* xpath_ast_cache_get_bc(const char* expr,
                                                    size_t expr_len);

/* Store a compiled bytecode in the cache slot for the given
 * expression. The cache takes ownership of `bc` and frees it when
 * the slot is overwritten. If the expression is not in the AST
 * cache, the bytecode is freed immediately and the function is a
 * no-op. */
void xpath_ast_cache_store_bc(const char* expr, size_t expr_len,
                               struct LeptrisXPathBytecode* bc);

#endif /* XPATH_INTERNAL_H */