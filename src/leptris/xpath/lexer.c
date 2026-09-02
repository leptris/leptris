/* lexer.c - XPath 1.0 Lexer (Pure C)
 * Copyright (c) 2024, Ribose Inc.
 */

#include "lexer.h"
#include "../leptris_internal.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>

/* Token type names for debugging */
const char* xpath_token_type_names[] = {
    "EOF",
    "SLASH",
    "DOUBLE_SLASH",
    "AT",
    "DOT",
    "DOUBLE_DOT",
    "LPAREN",
    "RPAREN",
    "LBRACKET",
    "RBRACKET",
    "COMMA",
    "DOUBLE_COLON",
    "NCNAME",
    "QNAME",
    "STRING",
    "NUMBER",
    "EQUALS",
    "NOT_EQUALS",
    "LT",
    "LE",
    "GT",
    "GE",
    "PLUS",
    "MINUS",
    "STAR",
    "PIPE",
    "AND",
    "OR",
    "DIV",
    "MOD",
    "ANCESTOR",
    "ANCESTOR_OR_SELF",
    "ATTRIBUTE",
    "CHILD",
    "DESCENDANT",
    "DESCENDANT_OR_SELF",
    "FOLLOWING",
    "FOLLOWING_SIBLING",
    "NAMESPACE",
    "PARENT",
    "PRECEDING",
    "PRECEDING_SIBLING",
    "SELF",
    "COMMENT",
    "TEXT",
    "PROCESSING_INSTRUCTION",
    "NODE",
    "DOLLAR",
    "VARIABLE_REFERENCE"
};

/* Forward declarations */
static void skip_whitespace(XPathLexer* lexer);
static int is_ncname_start(char c);
static int is_ncname_char(char c);
static XPathTokenType check_keyword(const char* str, size_t len, int peek_ahead, XPathLexer* lexer);

/* Create a new lexer */
XPathLexer* xpath_lexer_new(const char* input, size_t len) {
    if (!input) return NULL;

    XPathLexer* lexer = LEPTRIS_ALLOC(XPathLexer);
    if (!lexer) return NULL;

    lexer->input = input;
    lexer->pos = input;
    lexer->end = input + len;
    lexer->line = 1;
    lexer->column = 1;
    lexer->error_msg[0] = '\0';

    return lexer;
}

/* Free the lexer */
void xpath_lexer_free(XPathLexer* lexer) {
    if (lexer) {
        LEPTRIS_FREE(lexer);
    }
}

/* Check if character is NCName start character. Multi-byte UTF-8
 * sequences (every lead byte >= 0xC2) are name characters per the
 * NCName production (Namespaces 1.0: the Unicode base set) —
 * expressions like select="Ältestenrat" must lex. */
static int is_ncname_start(char c) {
    unsigned char u = (unsigned char)c;
    return isalpha(u) || u == '_' || u >= 0x80;
}

/* Check if character is NCName character */
static int is_ncname_char(char c) {
    unsigned char u = (unsigned char)c;
    return isalnum(u) || u == '_' || u == '-' || u == '.' || u >= 0x80;
}

/* Skip whitespace and update position */
static void skip_whitespace(XPathLexer* lexer) {
    while (lexer->pos < lexer->end && isspace((unsigned char)*lexer->pos)) {
        if (*lexer->pos == '\n') {
            lexer->line++;
            lexer->column = 1;
        } else {
            lexer->column++;
        }
        lexer->pos++;
    }
}

/* Check if a string matches a keyword and return appropriate token type */
static XPathTokenType check_keyword(const char* str, size_t len, int peek_ahead, XPathLexer* lexer) {
    /* Check for axis names (must be followed by ::) */
    if (peek_ahead && lexer->pos < lexer->end - 1 &&
        lexer->pos[0] == ':' && lexer->pos[1] == ':') {
        if (len == 8 && strncmp(str, "ancestor", 8) == 0) return TOK_ANCESTOR;
        if (len == 16 && strncmp(str, "ancestor-or-self", 16) == 0) return TOK_ANCESTOR_OR_SELF;
        if (len == 9 && strncmp(str, "attribute", 9) == 0) return TOK_ATTRIBUTE;
        if (len == 5 && strncmp(str, "child", 5) == 0) return TOK_CHILD;
        if (len == 10 && strncmp(str, "descendant", 10) == 0) return TOK_DESCENDANT;
        if (len == 18 && strncmp(str, "descendant-or-self", 18) == 0) return TOK_DESCENDANT_OR_SELF;
        if (len == 9 && strncmp(str, "following", 9) == 0) return TOK_FOLLOWING;
        if (len == 17 && strncmp(str, "following-sibling", 17) == 0) return TOK_FOLLOWING_SIBLING;
        if (len == 9 && strncmp(str, "namespace", 9) == 0) return TOK_NAMESPACE;
        if (len == 6 && strncmp(str, "parent", 6) == 0) return TOK_PARENT;
        if (len == 9 && strncmp(str, "preceding", 9) == 0) return TOK_PRECEDING;
        if (len == 17 && strncmp(str, "preceding-sibling", 17) == 0) return TOK_PRECEDING_SIBLING;
        if (len == 4 && strncmp(str, "self", 4) == 0) return TOK_SELF;
    }

    /* Check for node type tests (must be followed by '(') */
    if (peek_ahead && lexer->pos < lexer->end && *lexer->pos == '(') {
        if (len == 7 && strncmp(str, "comment", 7) == 0) return TOK_COMMENT;
        if (len == 4 && strncmp(str, "text", 4) == 0) return TOK_TEXT;
        if (len == 4 && strncmp(str, "node", 4) == 0) return TOK_NODE;
        if (len == 22 && strncmp(str, "processing-instruction", 22) == 0)
            return TOK_PROCESSING_INSTRUCTION;
    }

    /* Check for operator keywords (can appear anywhere) */
    if (len == 3 && strncmp(str, "and", 3) == 0) return TOK_AND;
    if (len == 2 && strncmp(str, "or", 2) == 0) return TOK_OR;
    if (len == 3 && strncmp(str, "div", 3) == 0) return TOK_DIV;
    if (len == 3 && strncmp(str, "mod", 3) == 0) return TOK_MOD;

    return TOK_NCNAME;
}

/* Get next token */
XPathToken xpath_lexer_next_token(XPathLexer* lexer) {
    XPathToken token;

    if (!lexer) {
        token.type = TOK_EOF;
        token.value = NULL;
        token.value_len = 0;
        token.line = 0;
        token.column = 0;
        return token;
    }

    skip_whitespace(lexer);

    token.line = lexer->line;
    token.column = lexer->column;

    if (lexer->pos >= lexer->end) {
        token.type = TOK_EOF;
        token.value = lexer->pos;
        token.value_len = 0;
        return token;
    }

    char c = *lexer->pos;

    /* Single character tokens */
    switch (c) {
        case '@':
            token.type = TOK_AT;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '(':
            token.type = TOK_LPAREN;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case ')':
            token.type = TOK_RPAREN;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '[':
            token.type = TOK_LBRACKET;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case ']':
            token.type = TOK_RBRACKET;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case ',':
            token.type = TOK_COMMA;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '?':
            /* 3.1 postfix lookup: ?key / ?integer. Was a hard lex
             * error — only previously-failing input changes. */
            token.type = TOK_QUESTION;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '#':
            /* 3.0 named function reference: name#arity. */
            token.type = TOK_HASH;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '+':
            token.type = TOK_PLUS;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '-':
            token.type = TOK_MINUS;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '*':
            token.type = TOK_STAR;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '|':
            /* XPath 3.0 string concat `||` before the union pipe. */
            if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '|') {
                token.type = TOK_CONCAT;
                token.value = lexer->pos;
                token.value_len = 2;
                lexer->pos += 2;
                lexer->column += 2;
                return token;
            }
            token.type = TOK_PIPE;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;

        case '$':
            /* Variable reference: $varname */
            token.type = TOK_DOLLAR;
            token.value = lexer->pos;
            token.value_len = 1;
            lexer->pos++;
            lexer->column++;
            return token;
    }

    /* Multi-character tokens */

    /* Slash and double slash */
    if (c == '/') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '/') {
            token.type = TOK_DOUBLE_SLASH;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        token.type = TOK_SLASH;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    /* Dot and double dot */
    if (c == '.') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '.') {
            token.type = TOK_DOUBLE_DOT;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        /* Check if it's a number like .5 */
        if (lexer->pos + 1 < lexer->end && isdigit((unsigned char)lexer->pos[1])) {
            goto parse_number;
        }
        token.type = TOK_DOT;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    /* Colon (for ::, :=, and QNames) */
    if (c == ':') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == ':') {
            token.type = TOK_DOUBLE_COLON;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        /* XPath 3.1 let binding separator `:=`. */
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '=') {
            token.type = TOK_ASSIGN;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        /* Map constructor key/value separator (3.1). A stray colon
         * was a hard lex error before — nothing that lexed cleanly
         * regresses. */
        token.type = TOK_COLON;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos += 1;
        lexer->column += 1;
        return token;
    }

    /* Comparison operators */
    if (c == '=') {
        /* XPath 3.1 arrow `=>` before plain equality. */
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '>') {
            token.type = TOK_ARROW;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        token.type = TOK_EQUALS;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    if (c == '!') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '=') {
            token.type = TOK_NOT_EQUALS;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        /* XPath 3.0 simple map `!`. */
        token.type = TOK_BANG;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    if (c == '<') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '=') {
            token.type = TOK_LE;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        token.type = TOK_LT;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    if (c == '>') {
        if (lexer->pos + 1 < lexer->end && lexer->pos[1] == '=') {
            token.type = TOK_GE;
            token.value = lexer->pos;
            token.value_len = 2;
            lexer->pos += 2;
            lexer->column += 2;
            return token;
        }
        token.type = TOK_GT;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    /* String literals */
    if (c == '\'' || c == '"') {
        char quote = c;
        const char* start = lexer->pos;
        lexer->pos++;
        lexer->column++;

        while (lexer->pos < lexer->end && *lexer->pos != quote) {
            if (*lexer->pos == '\n') {
                lexer->line++;
                lexer->column = 1;
            } else {
                lexer->column++;
            }
            lexer->pos++;
        }

        if (lexer->pos >= lexer->end) {
            if (lexer->input && start >= lexer->input) {
                size_t byte_offset = start - lexer->input;

                leptris_set_error_with_context(
                    LEPTRIS_ERROR_XPATH_SYNTAX,
                    "Unterminated string literal",
                    lexer->input,
                    byte_offset,
                    token.line,
                    token.column
                );
            }

            snprintf(lexer->error_msg, sizeof(lexer->error_msg),
                    "Unterminated string literal at line %d", token.line);
            token.type = TOK_EOF;  /* Error token */
            token.value = start;
            token.value_len = 0;
            return token;
        }

        lexer->pos++;  /* Skip closing quote */
        lexer->column++;

        token.type = TOK_STRING;
        token.value = start;
        token.value_len = lexer->pos - start;
        return token;
    }

    /* Numbers */
parse_number:
    if (isdigit((unsigned char)c) || c == '.') {
        const char* start = lexer->pos;

        while (lexer->pos < lexer->end &&
               (isdigit((unsigned char)*lexer->pos) || *lexer->pos == '.')) {
            lexer->pos++;
            lexer->column++;
        }

        token.type = TOK_NUMBER;
        token.value = start;
        token.value_len = lexer->pos - start;
        return token;
    }

    /* NCNames and QNames (and keywords) */
    if (is_ncname_start(c)) {
        const char* start = lexer->pos;

        while (lexer->pos < lexer->end && is_ncname_char(*lexer->pos)) {
            lexer->pos++;
            lexer->column++;
        }

        size_t len = lexer->pos - start;

        /* Check for QName (prefix:localname) */
        if (lexer->pos < lexer->end && *lexer->pos == ':' &&
            lexer->pos + 1 < lexer->end && lexer->pos[1] != ':') {  /* Not :: */

            lexer->pos++;  /* Skip : */
            lexer->column++;

            if (is_ncname_start(*lexer->pos)) {
                while (lexer->pos < lexer->end && is_ncname_char(*lexer->pos)) {
                    lexer->pos++;
                    lexer->column++;
                }

                token.type = TOK_QNAME;
                token.value = start;
                token.value_len = lexer->pos - start;
                return token;
            }
        }

        /* Check for keywords */
        token.type = check_keyword(start, len, 1, lexer);
        token.value = start;
        token.value_len = len;
        return token;
    }

    /* XPath 3.1 switch bodies: '{' and '}' (only the switch
     * parser consumes them; anything else errors downstream). */
    if (c == '{' || c == '}') {
        token.type = c == '{' ? TOK_LBRACE : TOK_RBRACE;
        token.value = lexer->pos;
        token.value_len = 1;
        lexer->pos++;
        lexer->column++;
        return token;
    }

    /* Invalid character */
    if (lexer->input && lexer->pos >= lexer->input) {
        size_t byte_offset = lexer->pos - lexer->input;
        char msg[256];
        snprintf(msg, sizeof(msg),
                "Invalid character '%c' in XPath expression",
                c);

        leptris_set_error_with_context(
            LEPTRIS_ERROR_XPATH_SYNTAX,
            msg,
            lexer->input,
            byte_offset,
            lexer->line,
            lexer->column
        );
    }

    snprintf(lexer->error_msg, sizeof(lexer->error_msg),
            "Invalid character '%c' at line %d, column %d",
            c, lexer->line, lexer->column);
    token.type = TOK_EOF;  /* Error token */
    token.value = lexer->pos;
    token.value_len = 0;
    return token;
}

/* Get error message */
const char* xpath_lexer_error(XPathLexer* lexer) {
    return lexer ? lexer->error_msg : "NULL lexer";
}

/* Convert token type to string */
const char* xpath_token_type_to_string(XPathTokenType type) {
    if (type >= 0 && type < sizeof(xpath_token_type_names) / sizeof(xpath_token_type_names[0])) {
        return xpath_token_type_names[type];
    }
    return "UNKNOWN";
}