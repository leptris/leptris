/* parser.h - XPath parser public API
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef XPATH_PARSER_H
#define XPATH_PARSER_H

#include "xpath_internal.h"

/* Forward declarations - types already defined in taurus_internal.h */
/* No need to redefine them here */

/* Create new parser */
XPathParser* xpath_parser_new(const char* input, size_t len);

/* Free parser resources */
void xpath_parser_free(XPathParser* parser);

/* Parse expression into AST (caller owns returned node) */
XPathASTNode* xpath_parse(XPathParser* parser);

/* Get error message (returns NULL if no error) */
const char* xpath_parser_error(XPathParser* parser);

/* Free AST node and all children recursively */
void ast_node_free(XPathASTNode* node);

#endif /* XPATH_PARSER_H */