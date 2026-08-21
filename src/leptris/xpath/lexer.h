/* lexer.h - XPath lexer public API
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef XPATH_LEXER_H
#define XPATH_LEXER_H

#include "xpath_internal.h"

/* Create new lexer */
XPathLexer* xpath_lexer_new(const char* input, size_t len);

/* Free lexer resources */
void xpath_lexer_free(XPathLexer* lexer);

/* Get next token (returns token by value, value field points into input) */
XPathToken xpath_lexer_next_token(XPathLexer* lexer);

/* Get error message */
const char* xpath_lexer_error(XPathLexer* lexer);

/* Convert token type to string (for debugging) */
const char* xpath_token_type_to_string(XPathTokenType type);

#endif /* XPATH_LEXER_H */