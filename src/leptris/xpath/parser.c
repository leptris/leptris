/* parser.c - XPath parser implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 parser.
 * Uses token buffer for lookahead (matches leptris_internal.h).
 */

#include "parser.h"
#include "lexer.h"
#include "evaluator_internal.h"  /* xpath_axis_from_name (TODO 113) */
#include "../leptris_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Expression parsers (precedence order) */
static XPathASTNode* parse_expr(XPathParser* parser);
static XPathASTNode* parse_or_expr(XPathParser* parser);
static XPathASTNode* parse_and_expr(XPathParser* parser);
static XPathASTNode* parse_equality_expr(XPathParser* parser);
static XPathASTNode* parse_relational_expr(XPathParser* parser);
static XPathASTNode* parse_additive_expr(XPathParser* parser);
static XPathASTNode* parse_multiplicative_expr(XPathParser* parser);
static XPathASTNode* parse_unary_expr(XPathParser* parser);
static XPathASTNode* parse_union_expr(XPathParser* parser);
static XPathASTNode* parse_arrow_expr(XPathParser* parser);

/* Path parsers */
static XPathASTNode* parse_path_expr(XPathParser* parser);
static XPathASTNode* parse_filter_expr(XPathParser* parser);
static XPathASTNode* parse_primary_expr(XPathParser* parser);
static XPathASTNode* parse_postfix_ops(XPathParser* parser,
                                       XPathASTNode* expr);
static XPathASTNode* parse_if_expr(XPathParser* parser);
static XPathASTNode* parse_for_expr(XPathParser* parser);
static XPathASTNode* parse_let_expr(XPathParser* parser);
/* Saxon-HE rejects switch in XPath EXPRESSIONS (XPST0003 — the
 * syntax is XSLT 3.0 PATTERN-only). parse_switch_expr lands with
 * pattern support (TODO.xslt-full/06). */
#if 0
static XPathASTNode* parse_switch_expr(XPathParser* parser);
#endif
static XPathASTNode* parse_location_path(XPathParser* parser);
static XPathASTNode* parse_relative_location_path(XPathParser* parser);
static XPathASTNode* parse_step(XPathParser* parser);

/* Node test and predicate parsers */
static XPathASTNode* parse_node_test(XPathParser* parser);
static XPathASTNode* parse_predicate(XPathParser* parser);
static XPathASTNode* parse_function_call(XPathParser* parser, const char* name, size_t name_len);

/* Token management */
static void advance_token(XPathParser* parser);
static XPathToken* peek_token(XPathParser* parser, int offset);
static XPathToken* current_token(XPathParser* parser);
static int current_token_is(XPathParser* parser, XPathTokenType type);
static int match_token(XPathParser* parser, XPathTokenType type);
static int consume_token(XPathParser* parser, XPathTokenType type, const char* error_msg);

/* AST helpers */
static char* token_to_string(const XPathToken* token);
static XPathASTNode* create_operator_node(XPathOperatorType op_type,
                                          XPathASTNode* left,
                                          XPathASTNode* right);

/* ============================================================================
 * Parser Lifecycle
 * ============================================================================ */

XPathParser* xpath_parser_new(const char* input, size_t len) {
    if (!input) return NULL;

    XPathParser* parser = LEPTRIS_ALLOC(XPathParser);
    if (!parser) return NULL;

    parser->lexer = xpath_lexer_new(input, len);
    if (!parser->lexer) {
        LEPTRIS_FREE(parser);
        return NULL;
    }

    parser->tokens = NULL;
    parser->token_count = 0;
    parser->token_pos = 0;
    parser->error_msg[0] = '\0';

    /* Tokenize entire input into array */
    size_t capacity = 16;
    parser->tokens = LEPTRIS_ALLOC_N(XPathToken, capacity);
    if (!parser->tokens) {
        xpath_lexer_free(parser->lexer);
        LEPTRIS_FREE(parser);
        return NULL;
    }

    /* Read all tokens */
    while (1) {
        XPathToken tok = xpath_lexer_next_token(parser->lexer);

        /* Grow array if needed */
        if (parser->token_count >= capacity) {
            capacity *= 2;
            XPathToken* new_tokens = LEPTRIS_REALLOC_N(parser->tokens, XPathToken, capacity);
            if (!new_tokens) {
                LEPTRIS_FREE(parser->tokens);
                xpath_lexer_free(parser->lexer);
                LEPTRIS_FREE(parser);
                return NULL;
            }
            parser->tokens = new_tokens;
        }

        parser->tokens[parser->token_count++] = tok;

        if (tok.type == TOK_EOF) break;
    }

    return parser;
}

void xpath_parser_free(XPathParser* parser) {
    if (!parser) return;
    if (parser->tokens) {
        LEPTRIS_FREE(parser->tokens);
    }
    if (parser->lexer) {
        xpath_lexer_free(parser->lexer);
    }
    LEPTRIS_FREE(parser);
}

const char* xpath_parser_error(XPathParser* parser) {
    if (!parser) return "Invalid parser";
    return parser->error_msg[0] ? parser->error_msg : NULL;
}

/* ============================================================================
 * AST Node Management
 * ============================================================================ */

static XPathASTNode* ast_node_new(XPathASTType type) {
    XPathASTNode* node = LEPTRIS_ALLOC(XPathASTNode);
    if (!node) return NULL;

    node->type = type;
    node->value = NULL;
    node->number_value = 0.0;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    node->axis_id = XPATH_AXIS_CHILD;

    /* Initialize namespace support fields (v0.8.0) */
    node->prefix = NULL;
    node->local_name = NULL;

    return node;
}

void ast_node_free(XPathASTNode* node) {
    if (!node) return;

    if (node->value) {
        LEPTRIS_FREE(node->value);
    }

    /* Free namespace support fields (v0.8.0) */
    if (node->prefix) {
        LEPTRIS_FREE(node->prefix);
    }
    if (node->local_name) {
        LEPTRIS_FREE(node->local_name);
    }

    if (node->children) {
        for (size_t i = 0; i < node->child_count; i++) {
            ast_node_free(node->children[i]);
        }
        LEPTRIS_FREE(node->children);
    }

    LEPTRIS_FREE(node);
}

static void ast_node_add_child(XPathASTNode* parent, XPathASTNode* child) {
    if (!parent || !child) return;

    /* Resize if needed */
    if (parent->child_count >= parent->child_capacity) {
        size_t new_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        XPathASTNode** new_children = LEPTRIS_REALLOC_N(parent->children, XPathASTNode*, new_capacity);
        if (!new_children) return;
        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    parent->children[parent->child_count++] = child;
}

/* ============================================================================
 * Token Management
 * ============================================================================ */

static XPathToken* current_token(XPathParser* parser) {
    if (!parser || parser->token_pos >= parser->token_count) return NULL;
    return &parser->tokens[parser->token_pos];
}

static void advance_token(XPathParser* parser) {
    if (!parser) return;
    if (parser->token_pos < parser->token_count) {
        parser->token_pos++;
    }
}

static XPathToken* peek_token(XPathParser* parser, int offset) {
    if (!parser) return NULL;
    size_t pos = parser->token_pos + offset;
    if (pos >= parser->token_count) return NULL;
    return &parser->tokens[pos];
}

static int current_token_is(XPathParser* parser, XPathTokenType type) {
    XPathToken* tok = current_token(parser);
    return tok && (int)tok->type == (int)type;
}

static int match_token(XPathParser* parser, XPathTokenType type) {
    if (!parser) return 0;
    if (current_token_is(parser, type)) {
        advance_token(parser);
        return 1;
    }
    return 0;
}

static int consume_token(XPathParser* parser, XPathTokenType type, const char* error_msg) {
    if (!parser) return 0;
    XPathToken* tok = current_token(parser);
    if (tok && (int)tok->type == (int)type) {
        advance_token(parser);
        return 1;
    }
    if (tok && parser->lexer && parser->lexer->input) {
        /* Calculate byte offset from token position */
        size_t byte_offset = (tok->value && tok->value >= parser->lexer->input)
            ? tok->value - parser->lexer->input
            : 0;

        /* Build detailed error message */
        char detailed_msg[512];
        snprintf(detailed_msg, sizeof(detailed_msg),
                 "%s (got %s)",
                 error_msg,
                 xpath_token_type_to_string(tok->type));

        leptris_set_error_with_context(
            LEPTRIS_ERROR_XPATH_SYNTAX,
            detailed_msg,
            parser->lexer->input,
            byte_offset,
            tok->line,
            tok->column
        );

        /* Also store in parser for legacy compatibility */
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "%s at line %d, column %d (got %s)",
                 error_msg, tok->line, tok->column,
                 xpath_token_type_to_string(tok->type));
    } else if (parser->lexer && parser->lexer->input) {
        size_t byte_offset = (parser->lexer->end && parser->lexer->end >= parser->lexer->input)
            ? parser->lexer->end - parser->lexer->input
            : 0;

        leptris_set_error_with_context(
            LEPTRIS_ERROR_XPATH_SYNTAX,
            error_msg,
            parser->lexer->input,
            byte_offset,
            parser->lexer->line,
            parser->lexer->column
        );

        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "%s at EOF", error_msg);
    } else {
        /* Fallback: set error without context */
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "%s", error_msg);
    }
    return 0;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static char* token_to_string(const XPathToken* token) {
    if (!token || token->value_len == 0) return NULL;

    char* str = LEPTRIS_ALLOC_N(char, token->value_len + 1);
    if (!str) return NULL;

    memcpy(str, token->value, token->value_len);
    str[token->value_len] = '\0';
    return str;
}

static XPathASTNode* create_operator_node(XPathOperatorType op_type,
                                          XPathASTNode* left,
                                          XPathASTNode* right) {
    XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
    if (!node) {
        ast_node_free(left);
        ast_node_free(right);
        return NULL;
    }

    node->number_value = (double)op_type;
    ast_node_add_child(node, left);
    ast_node_add_child(node, right);
    return node;
}

/* ============================================================================
 * Main Parser Entry Point
 * ============================================================================ */

static int ncname_is(XPathToken* t, const char* kw, size_t kwlen) {
    return t && t->type == TOK_NCNAME && t->value_len == kwlen &&
           memcmp(t->value, kw, kwlen) == 0;
}

/* SequenceType v1 (TODO.xslt-full/06): a prefixed name arrives as
 * one TOK_QNAME (xs:integer); a bare NCNAME may be a KindTest
 * (node()/item()) with an argument list. Occurrence indicators:
 * '*' and '+' only ('?' has no lexer token). Returns a malloc'd
 * "name[()]" + indicator string, NULL on parse failure. */
/* Can this token begin a primary/unary expression? Decides whether
 * `*`/`+` after a SequenceType is an occurrence indicator or the
 * multiplication/addition operator (`'12' cast as xs:integer + 1`
 * — the operator; `'a' instance of xs:string*` at EOF — the
 * indicator). */
static int token_begins_expr(XPathTokenType t) {
    switch (t) {
        case TOK_NUMBER: case TOK_STRING: case TOK_NCNAME: case TOK_QNAME:
        case TOK_DOLLAR: case TOK_LPAREN: case TOK_DOT: case TOK_DOUBLE_DOT:
        case TOK_SLASH: case TOK_DOUBLE_SLASH: case TOK_AT: case TOK_MINUS:
        case TOK_NODE: case TOK_TEXT: case TOK_COMMENT:
        case TOK_PROCESSING_INSTRUCTION:
            return 1;
        default:
            return 0;
    }
}

static char* parse_sequence_type(XPathParser* parser) {
    XPathToken* t = current_token(parser);
    /* The lexer keyword-tokenizes node tests — `text()`,
     * `comment()`, `processing-instruction()` are type names here
     * too (issue #744). */
    if (!t || (t->type != TOK_NCNAME && t->type != TOK_QNAME &&
               t->type != TOK_NODE && t->type != TOK_TEXT &&
               t->type != TOK_COMMENT &&
               t->type != TOK_PROCESSING_INSTRUCTION))
        return NULL;
    char buf[96];
    size_t len = t->value_len;
    if (len >= sizeof(buf) - 4) return NULL;
    memcpy(buf, t->value, len);
    buf[len] = '\0';
    advance_token(parser);
    /* KindTest argument list: node() / item(). */
    if (current_token_is(parser, TOK_LPAREN)) {
        advance_token(parser);
        if (current_token_is(parser, TOK_RPAREN)) advance_token(parser);
        else return NULL;
        if (len + 2 < sizeof(buf) - 4) { buf[len++] = '('; buf[len++] = ')'; buf[len] = '\0'; }
    }
    /* Occurrence indicators. `?` is unambiguous (postfix lookup
     * applies to primary exprs, never type names); `*`/`+` need
     * lookahead (#744). */
    if (current_token_is(parser, TOK_QUESTION)) {
        if (len + 1 < sizeof(buf) - 1) { buf[len++] = '?'; buf[len] = 0; }
        advance_token(parser);
    } else if (current_token_is(parser, TOK_STAR) ||
               current_token_is(parser, TOK_PLUS)) {
        XPathTokenType oc = current_token(parser)->type;
        XPathToken* after = (parser->token_pos + 1 < parser->token_count)
                                ? &parser->tokens[parser->token_pos + 1] : NULL;
        if (!after || !token_begins_expr(after->type)) {
            if (len + 1 < sizeof(buf) - 1) {
                buf[len++] = (oc == TOK_STAR) ? '*' : '+';
                buf[len] = 0;
            }
            advance_token(parser);
        }
    }
    char* out = LEPTRIS_ALLOC_N(char, len + 1);
    if (out) memcpy(out, buf, len + 1);
    return out;
}

static XPathASTNode* parse_expr(XPathParser* parser) {
    XPathASTNode* e = parse_or_expr(parser);
    if (!e) return NULL;
    /* XPath 2.0+ range `A to B` (XSLT 3.0): `to` between two
     * expressions. Value-matched so name tests stay intact. */
    if (ncname_is(current_token(parser), "to", 2)) {
        advance_token(parser);
        XPathASTNode* hi = parse_or_expr(parser);
        if (!hi) { ast_node_free(e); return NULL; }
        XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
        if (!node) { ast_node_free(e); ast_node_free(hi); return NULL; }
        node->number_value = (double)XPATH_OP_RANGE;
        ast_node_add_child(node, e);
        ast_node_add_child(node, hi);
        return node;
    }
    /* 2.0 type operators (TODO.xslt-full/06): value-matched
     * two-word keywords + a SequenceType. Same whole-expr shape as
     * `to` above (X instance of T — comparisons against the result
     * need parentheses, as with ranges). */
    {
        XPathOperatorType op;
        int matched = 0;
        XPathToken* t = current_token(parser);
        XPathToken* t2 = (t && parser->token_pos + 1 < parser->token_count)
                             ? &parser->tokens[parser->token_pos + 1] : NULL;
        if (ncname_is(t, "instance", 8) &&
            ncname_is(t2, "of", 2)) {
            op = XPATH_OP_INSTANCE_OF; matched = 1;
            advance_token(parser); advance_token(parser);
        } else if (ncname_is(t, "castable", 8) &&
                   ncname_is(t2, "as", 2)) {
            op = XPATH_OP_CASTABLE; matched = 1;
            advance_token(parser); advance_token(parser);
        } else if (ncname_is(t, "cast", 4) &&
                   ncname_is(t2, "as", 2)) {
            op = XPATH_OP_CAST; matched = 1;
            advance_token(parser); advance_token(parser);
        } else if (ncname_is(t, "treat", 5) &&
                   ncname_is(t2, "as", 2)) {
            op = XPATH_OP_TREAT; matched = 1;
            advance_token(parser); advance_token(parser);
        }
        if (matched) {
            char* ty = parse_sequence_type(parser);
            if (!ty) { ast_node_free(e); return NULL; }
            XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
            if (!node) { free(ty); ast_node_free(e); return NULL; }
            node->number_value = (double)op;
            node->value = ty;
            ast_node_add_child(node, e);
            /* The type-op node closes above the additive level, so
             * trailing arithmetic on the cast result must fold
             * here: `'12' cast as xs:integer + 1` (#744). */
            while (current_token_is(parser, TOK_PLUS) ||
                   current_token_is(parser, TOK_MINUS) ||
                   current_token_is(parser, TOK_STAR)) {
                XPathTokenType ot = current_token(parser)->type;
                advance_token(parser);
                XPathASTNode* rhs = parse_or_expr(parser);
                if (!rhs) { ast_node_free(node); return NULL; }
                XPathASTNode* opn = ast_node_new(XPATH_AST_OPERATOR);
                if (!opn) { ast_node_free(rhs); ast_node_free(node); return NULL; }
                opn->number_value = (double)((ot == TOK_PLUS)   ? XPATH_OP_PLUS
                                           : (ot == TOK_MINUS)  ? XPATH_OP_MINUS
                                                                : XPATH_OP_MULTIPLY);
                ast_node_add_child(opn, node);
                ast_node_add_child(opn, rhs);
                node = opn;
            }
            return node;
        }
    }
    return e;
}

XPathASTNode* xpath_parse(XPathParser* parser) {
    if (!parser) return NULL;

    XPathASTNode* ast = parse_expr(parser);
    /* XPath 3.1 top-level sequence: Expr ::= ExprSingle ("," ExprSingle)*.
     * One member parses as itself; a comma list builds the sequence
     * node (synthetic-text items) the parenthesized form uses. */
    if (ast && current_token_is(parser, TOK_COMMA)) {
        XPathASTNode* seq = ast_node_new(XPATH_AST_OPERATOR);
        if (!seq) { ast_node_free(ast); return NULL; }
        seq->number_value = (double)XPATH_OP_SEQUENCE;
        ast_node_add_child(seq, ast);
        while (current_token_is(parser, TOK_COMMA)) {
            advance_token(parser);
            XPathASTNode* member = parse_expr(parser);
            if (!member) { ast_node_free(seq); return NULL; }
            ast_node_add_child(seq, member);
        }
        ast = seq;
    }

    if (ast && !current_token_is(parser, TOK_EOF)) {
        XPathToken* tok = current_token(parser);
        if (tok && parser->lexer && parser->lexer->input) {
            size_t byte_offset = (tok->value && tok->value >= parser->lexer->input)
                ? tok->value - parser->lexer->input
                : 0;
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Unexpected token after expression: %s",
                     xpath_token_type_to_string(tok->type));

            leptris_set_error_with_context(
                LEPTRIS_ERROR_XPATH_SYNTAX,
                msg,
                parser->lexer->input,
                byte_offset,
                tok->line,
                tok->column
            );

            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Unexpected token after expression: %s at line %d, column %d",
                     xpath_token_type_to_string(tok->type),
                     tok->line, tok->column);
        } else {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Unexpected token after expression");
        }
        ast_node_free(ast);
        return NULL;
    }

    return ast;
}

/* ============================================================================
 * Expression Parsers (Operator Precedence)
 * ============================================================================ */

/* Parse OR expressions: AndExpr ( 'or' AndExpr )* */
static XPathASTNode* parse_or_expr(XPathParser* parser) {
    if (!parser) return NULL;

    XPathASTNode* left = parse_and_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_OR)) {
        advance_token(parser);
        XPathASTNode* right = parse_and_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(XPATH_OP_OR, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse AND expressions: EqualityExpr ( 'and' EqualityExpr )* */
static XPathASTNode* parse_and_expr(XPathParser* parser) {
    if (!parser) return NULL;

    XPathASTNode* left = parse_equality_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_AND)) {
        advance_token(parser);
        XPathASTNode* right = parse_equality_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(XPATH_OP_AND, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse equality expressions: RelationalExpr ( ('=' | '!=') RelationalExpr )* */
static XPathASTNode* parse_equality_expr(XPathParser* parser) {
    if (!parser) return NULL;

    XPathASTNode* left = parse_relational_expr(parser);
    if (!left) return NULL;

    while (1) {
        XPathOperatorType op_type;

        if (current_token_is(parser, TOK_EQUALS)) {
            op_type = XPATH_OP_EQUAL;
        } else if (current_token_is(parser, TOK_NOT_EQUALS)) {
            op_type = XPATH_OP_NOT_EQUAL;
        } else {
            break;
        }

        advance_token(parser);
        XPathASTNode* right = parse_relational_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(op_type, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse relational expressions: AdditiveExpr ( ('<' | '>' | '<=' | '>=') AdditiveExpr )* */
/* XPath 3.0 StringConcatExpr: AdditiveExpr ('||' AdditiveExpr)* —
 * sits between comparison and addition (XQuery/XPath 3.x
 * grammar). */
static XPathASTNode* parse_string_concat_expr(XPathParser* parser) {
    XPathASTNode* left = parse_additive_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_CONCAT)) {
        advance_token(parser);
        XPathASTNode* right = parse_additive_expr(parser);
        if (!right) { ast_node_free(left); return NULL; }
        left = create_operator_node(XPATH_OP_CONCAT, left, right);
        if (!left) return NULL;
    }
    return left;
}

static XPathASTNode* parse_relational_expr(XPathParser* parser) {
    XPathASTNode* left = parse_string_concat_expr(parser);
    if (!left) return NULL;

    while (1) {
        XPathOperatorType op_type;

        if (current_token_is(parser, TOK_LT)) {
            op_type = XPATH_OP_LESS;
        } else if (current_token_is(parser, TOK_LE)) {
            op_type = XPATH_OP_LESS_EQUAL;
        } else if (current_token_is(parser, TOK_GT)) {
            op_type = XPATH_OP_GREATER;
        } else if (current_token_is(parser, TOK_GE)) {
            op_type = XPATH_OP_GREATER_EQUAL;
        } else {
            break;
        }

        advance_token(parser);
        XPathASTNode* right = parse_string_concat_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(op_type, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse additive expressions: MultiplicativeExpr ( ('+' | '-') MultiplicativeExpr )* */
static XPathASTNode* parse_additive_expr(XPathParser* parser) {
    XPathASTNode* left = parse_multiplicative_expr(parser);
    if (!left) return NULL;

    while (1) {
        XPathOperatorType op_type;

        if (current_token_is(parser, TOK_PLUS)) {
            op_type = XPATH_OP_PLUS;
        } else if (current_token_is(parser, TOK_MINUS)) {
            op_type = XPATH_OP_MINUS;
        } else {
            break;
        }

        advance_token(parser);
        XPathASTNode* right = parse_multiplicative_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(op_type, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse multiplicative expressions: UnaryExpr ( ('*' | 'div' | 'mod') UnaryExpr )* */
static XPathASTNode* parse_multiplicative_expr(XPathParser* parser) {
    XPathASTNode* left = parse_unary_expr(parser);
    if (!left) return NULL;

    while (1) {
        XPathOperatorType op_type;

        if (current_token_is(parser, TOK_STAR)) {
            op_type = XPATH_OP_MULTIPLY;
        } else if (current_token_is(parser, TOK_DIV)) {
            op_type = XPATH_OP_DIV;
        } else if (current_token_is(parser, TOK_MOD)) {
            op_type = XPATH_OP_MOD;
        } else {
            break;
        }

        advance_token(parser);
        XPathASTNode* right = parse_unary_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(op_type, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* Parse unary expressions: '-' UnaryExpr | UnionExpr */
static XPathASTNode* parse_unary_expr(XPathParser* parser) {
    if (current_token_is(parser, TOK_MINUS)) {
        advance_token(parser);
        XPathASTNode* expr = parse_unary_expr(parser);
        if (!expr) return NULL;

        XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
        if (!node) {
            ast_node_free(expr);
            return NULL;
        }

        node->number_value = (double)XPATH_OP_NEGATION;
        ast_node_add_child(node, expr);
        return node;
    }

    return parse_arrow_expr(parser);
}

/* Parse union expressions: PathExpr ( '|' PathExpr )* — with the
 * XPath 3.0 simple map binding tighter than '|':
 * SimpleMapExpr := PathExpr ('!' PathExpr)*. */
static XPathASTNode* parse_simple_map_expr(XPathParser* parser) {
    XPathASTNode* left = parse_path_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_BANG)) {
        advance_token(parser);
        XPathASTNode* right = parse_path_expr(parser);
        if (!right) { ast_node_free(left); return NULL; }
        XPathASTNode* map = ast_node_new(XPATH_AST_OPERATOR);
        if (!map) { ast_node_free(left); ast_node_free(right); return NULL; }
        map->number_value = (double)XPATH_OP_MAP;
        ast_node_add_child(map, left);
        ast_node_add_child(map, right);
        left = map;
    }
    return left;
}

static XPathASTNode* parse_union_expr(XPathParser* parser) {
    XPathASTNode* left = parse_simple_map_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_PIPE)) {
        advance_token(parser);
        XPathASTNode* right = parse_simple_map_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(XPATH_OP_UNION, left, right);
        if (!left) return NULL;
    }

    return left;
}

/* XPath 3.1 ArrowExpr: UnionExpr ( '=>' Name '(' args ')' )* — the
 * accumulated left side becomes the FIRST argument of each call,
 * so `E => f(a) => g(b)` nests g(f(E, a), b). Arrow binds looser
 * than '|' and '!'. */
static XPathASTNode* parse_arrow_expr(XPathParser* parser) {
    XPathASTNode* left = parse_union_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_ARROW)) {
        advance_token(parser);
        XPathToken* nt = current_token(parser);
        if (!nt || (nt->type != TOK_NCNAME && nt->type != TOK_QNAME)) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected function name after '=>'");
            ast_node_free(left);
            return NULL;
        }
        char* fname = token_to_string(nt);
        if (!fname) { ast_node_free(left); return NULL; }
        advance_token(parser);

        XPathASTNode* call = ast_node_new(XPATH_AST_FUNCTION_CALL);
        if (!call) { free(fname); ast_node_free(left); return NULL; }
        call->value = fname;
        ast_node_add_child(call, left);   /* the arrow's first argument */

        if (!consume_token(parser, TOK_LPAREN,
                           "Expected '(' after arrow function name")) {
            ast_node_free(call);
            return NULL;
        }
        if (!current_token_is(parser, TOK_RPAREN)) {
            do {
                XPathASTNode* arg = parse_expr(parser);
                if (!arg) { ast_node_free(call); return NULL; }
                ast_node_add_child(call, arg);
            } while (match_token(parser, TOK_COMMA));
        }
        if (!consume_token(parser, TOK_RPAREN,
                           "Expected ')' after arrow arguments")) {
            ast_node_free(call);
            return NULL;
        }
        left = call;
    }
    return left;
}

/* ============================================================================
 * Path Parsers
 * ============================================================================ */

/* Parse path expressions */
static XPathASTNode* parse_path_expr(XPathParser* parser) {
    /* Check if it starts with location path indicator */
    if (current_token_is(parser, TOK_SLASH) ||
        current_token_is(parser, TOK_DOUBLE_SLASH) ||
        current_token_is(parser, TOK_AT) ||
        current_token_is(parser, TOK_DOT) ||
        current_token_is(parser, TOK_DOUBLE_DOT) ||
        current_token_is(parser, TOK_STAR) ||
        (current_token(parser) && current_token(parser)->type >= TOK_ANCESTOR &&
         current_token(parser)->type <= TOK_SELF)) {
        return parse_location_path(parser);
    }

    /* XPath 1.0 §3.7 lexical disambiguation: an operator KEYWORD at
     * the START of a path expression can only be a NameTest (an
     * element named div/mod/and/or — patterns like match="div" and
     * union patterns div|obj). As operators they need a left operand,
     * which a path start cannot supply. parse_node_test already
     * accepts the keyword tokens as names. */
    if (current_token_is(parser, TOK_DIV) ||
        current_token_is(parser, TOK_MOD) ||
        current_token_is(parser, TOK_AND) ||
        current_token_is(parser, TOK_OR)) {
        return parse_location_path(parser);
    }

    /* 3.1 square array constructor `[ a, b, ... ]` — a leading '['
     * can only be this (predicates need a preceding expression). */
    if (current_token_is(parser, TOK_LBRACKET)) {
        advance_token(parser);
        XPathASTNode* ac = ast_node_new(XPATH_AST_OPERATOR);
        if (!ac) return NULL;
        ac->number_value = (double)XPATH_OP_ARRAY_CONSTRUCTOR;
        while (!current_token_is(parser, TOK_RBRACKET)) {
            XPathASTNode* m = parse_expr(parser);
            if (!m) { ast_node_free(ac); return NULL; }
            ast_node_add_child(ac, m);
            if (current_token_is(parser, TOK_COMMA)) advance_token(parser);
            else break;
        }
        if (!current_token_is(parser, TOK_RBRACKET)) {
            ast_node_free(ac);
            return NULL;
        }
        advance_token(parser);
        return parse_postfix_ops(parser, ac);
    }

    /* Check for relative paths starting with NCName/QName */
    if (current_token_is(parser, TOK_NCNAME) || current_token_is(parser, TOK_QNAME)) {
        XPathToken* next = peek_token(parser, 1);

        /* `for $v ...` / `let $v := ...` (XSLT 3.0 / XPath 3.1) —
         * fall through to the primary's hooks before the
         * location-path dispatch eats the keyword as a name test. */
        if (current_token(parser)->type == TOK_NCNAME &&
            ((current_token(parser)->value_len == 3 &&
              memcmp(current_token(parser)->value, "for", 3) == 0) ||
             (current_token(parser)->value_len == 3 &&
              memcmp(current_token(parser)->value, "let", 3) == 0)) &&
            next && next->type == TOK_DOLLAR) {
            /* Fall through to filter expression */
        }
        /* 3.0 inline function item `function ($a, $b) { body }`. */
        else if (current_token(parser)->type == TOK_NCNAME &&
                 current_token(parser)->value_len == 8 &&
                 memcmp(current_token(parser)->value, "function", 8) == 0 &&
                 next && next->type == TOK_LPAREN) {
            advance_token(parser);
            advance_token(parser);
            char params[256];
            size_t plen = 0;
            params[0] = 0;
            while (!current_token_is(parser, TOK_RPAREN)) {
                if (!current_token_is(parser, TOK_DOLLAR)) return NULL;
                advance_token(parser);
                XPathToken* pt = current_token(parser);
                if (!pt || (pt->type != TOK_NCNAME &&
                            pt->type != TOK_QNAME))
                    return NULL;
                if (plen && plen + 1 < sizeof(params))
                    params[plen++] = '\x01';   /* matches the DYN_CALL splitter */
                if (plen + pt->value_len < sizeof(params)) {
                    memcpy(params + plen, pt->value, pt->value_len);
                    plen += pt->value_len;
                }
                params[plen] = 0;
                advance_token(parser);
                if (current_token_is(parser, TOK_COMMA))
                    advance_token(parser);
                else break;
            }
            if (!current_token_is(parser, TOK_RPAREN)) return NULL;
            advance_token(parser);
            if (!current_token_is(parser, TOK_LBRACE)) return NULL;
            advance_token(parser);
            XPathASTNode* body = parse_expr(parser);
            if (!body) return NULL;
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(body);
                return NULL;
            }
            advance_token(parser);
            XPathASTNode* fn = ast_node_new(XPATH_AST_OPERATOR);
            if (!fn) { ast_node_free(body); return NULL; }
            fn->number_value = (double)XPATH_OP_INLINE_FN;
            fn->value = leptris_strdup(params);
            ast_node_add_child(fn, body);
            return parse_postfix_ops(parser, fn);
        }
        /* 3.0 named function reference `name#arity`. */
        else if (current_token(parser)->type == TOK_NCNAME &&
                 next && next->type == TOK_HASH) {
            char name[128];
            size_t nl = current_token(parser)->value_len;
            if (nl >= sizeof(name)) return NULL;
            memcpy(name, current_token(parser)->value, nl);
            name[nl] = 0;
            advance_token(parser);
            advance_token(parser);
            XPathToken* at = current_token(parser);
            if (!at || at->type != TOK_NUMBER) return NULL;
            char ref[160];
            snprintf(ref, sizeof(ref), "%.*s#%.*s",
                     (int)nl, name, (int)at->value_len, at->value);
            advance_token(parser);
            XPathASTNode* fr = ast_node_new(XPATH_AST_OPERATOR);
            if (!fr) return NULL;
            fr->number_value = (double)XPATH_OP_FN_REF;
            fr->value = leptris_strdup(ref);
            return parse_postfix_ops(parser, fr);
        }
        /* 3.1 map constructor `map { k: v, ... }`. */
        else if (current_token(parser)->type == TOK_NCNAME &&
                 current_token(parser)->value_len == 3 &&
                 memcmp(current_token(parser)->value, "map", 3) == 0 &&
                 next && next->type == TOK_LBRACE) {
            advance_token(parser);
            advance_token(parser);
            XPathASTNode* mc = ast_node_new(XPATH_AST_OPERATOR);
            if (!mc) return NULL;
            mc->number_value = (double)XPATH_OP_MAP_CONSTRUCTOR;
            while (!current_token_is(parser, TOK_RBRACE)) {
                XPathASTNode* k = parse_expr(parser);
                if (!k || !current_token_is(parser, TOK_COLON)) {
                    ast_node_free(k);
                    ast_node_free(mc);
                    return NULL;
                }
                advance_token(parser);
                XPathASTNode* v = parse_expr(parser);
                if (!v) {
                    ast_node_free(k);
                    ast_node_free(mc);
                    return NULL;
                }
                ast_node_add_child(mc, k);
                ast_node_add_child(mc, v);
                if (current_token_is(parser, TOK_COMMA)) advance_token(parser);
                else break;
            }
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(mc);
                return NULL;
            }
            advance_token(parser);
            return parse_postfix_ops(parser, mc);
        }
        /* XQuery 1.0 computed constructors (TODO.xslt-full/11):
         * element NAME { content }, attribute NAME { value },
         * text { content }. Matched only with the name+brace in
         * place — bare `element`/`text` name tests stay intact
         * (the lexer only keyword-tokenizes `text(`). */
        else if (current_token(parser)->type == TOK_NCNAME &&
                 next && next->type == TOK_LBRACE &&
                 current_token(parser)->value_len == 8 &&
                 memcmp(current_token(parser)->value, "document", 8) == 0) {
            advance_token(parser);
            advance_token(parser);
            XPathASTNode* dc = ast_node_new(XPATH_AST_OPERATOR);
            if (!dc) return NULL;
            dc->number_value = (double)XPATH_OP_DOCUMENT_CTOR;
            for (;;) {
                XPathASTNode* item = parse_expr(parser);
                if (!item) { ast_node_free(dc); return NULL; }
                ast_node_add_child(dc, item);
                if (current_token_is(parser, TOK_COMMA)) {
                    advance_token(parser);
                    continue;
                }
                break;
            }
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(dc);
                return NULL;
            }
            advance_token(parser);
            return parse_postfix_ops(parser, dc);
        }
        /* XQuery 3.0 try/catch (#692): try { E } catch TEST { E }+.
         * TEST: * | err:CODE | err:* */
        else if (current_token(parser)->type == TOK_NCNAME &&
                 next && next->type == TOK_LBRACE &&
                 current_token(parser)->value_len == 3 &&
                 memcmp(current_token(parser)->value, "try", 3) == 0) {
            advance_token(parser);
            advance_token(parser);
            XPathASTNode* body = parse_expr(parser);
            if (!body) return NULL;
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(body);
                return NULL;
            }
            advance_token(parser);
            XPathASTNode* tc = ast_node_new(XPATH_AST_OPERATOR);
            if (!tc) { ast_node_free(body); return NULL; }
            tc->number_value = (double)XPATH_OP_TRY;
            ast_node_add_child(tc, body);
            char tests[256];
            size_t tlen = 0;
            tests[0] = 0;
            while (current_token_is(parser, TOK_NCNAME) &&
                   current_token(parser)->value_len == 5 &&
                   memcmp(current_token(parser)->value, "catch", 5) == 0) {
                advance_token(parser);
                char test[128];
                size_t tnl = 0;
                test[0] = 0;
                XPathToken* t = current_token(parser);
                if (t && t->type == TOK_STAR) {
                    test[tnl++] = '*';
                    advance_token(parser);
                } else if (t && (t->type == TOK_NCNAME ||
                                 t->type == TOK_QNAME)) {
                    if (t->value_len < sizeof(test)) {
                        memcpy(test, t->value, t->value_len);
                        tnl = t->value_len;
                    }
                    advance_token(parser);
                    if (current_token_is(parser, TOK_COLON)) {
                        advance_token(parser);
                        if (tnl < sizeof(test) - 1) test[tnl++] = ':';
                        t = current_token(parser);
                        if (t && t->type == TOK_STAR) {
                            if (tnl < sizeof(test) - 1) test[tnl++] = '*';
                            advance_token(parser);
                        } else if (t && (t->type == TOK_NCNAME ||
                                         t->type == TOK_QNAME)) {
                            if (tnl + t->value_len < sizeof(test)) {
                                memcpy(test + tnl, t->value, t->value_len);
                                tnl += t->value_len;
                            }
                            advance_token(parser);
                        }
                    }
                }
                if (!tnl) {
                    ast_node_free(tc);
                    return NULL;
                }
                test[tnl] = 0;
                if (tlen && tlen + 1 < sizeof(tests))
                    tests[tlen++] = '\x01';
                if (tlen + tnl < sizeof(tests)) {
                    memcpy(tests + tlen, test, tnl);
                    tlen += tnl;
                }
                tests[tlen] = 0;
                if (!current_token_is(parser, TOK_LBRACE)) {
                    ast_node_free(tc);
                    return NULL;
                }
                advance_token(parser);
                XPathASTNode* cb = parse_expr(parser);
                if (!cb) { ast_node_free(tc); return NULL; }
                if (!current_token_is(parser, TOK_RBRACE)) {
                    ast_node_free(cb);
                    ast_node_free(tc);
                    return NULL;
                }
                advance_token(parser);
                ast_node_add_child(tc, cb);
            }
            if (tc->child_count < 2) {   /* try needs >= 1 catch */
                ast_node_free(tc);
                return NULL;
            }
            tc->value = leptris_strdup(tests);
            return parse_postfix_ops(parser, tc);
        }
        else if (current_token(parser)->type == TOK_NCNAME &&
                 next && next->type == TOK_LBRACE &&
                 current_token(parser)->value_len == 4 &&
                 memcmp(current_token(parser)->value, "text", 4) == 0) {
            advance_token(parser);
            advance_token(parser);
            XPathASTNode* body = parse_expr(parser);
            if (!body) return NULL;
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(body);
                return NULL;
            }
            advance_token(parser);
            XPathASTNode* tc = ast_node_new(XPATH_AST_OPERATOR);
            if (!tc) { ast_node_free(body); return NULL; }
            tc->number_value = (double)XPATH_OP_TEXT_CTOR;
            ast_node_add_child(tc, body);
            return parse_postfix_ops(parser, tc);
        }
        else if (current_token(parser)->type == TOK_NCNAME &&
                 ((current_token(parser)->value_len == 7 &&
                   memcmp(current_token(parser)->value, "element", 7) == 0) ||
                  (current_token(parser)->value_len == 9 &&
                   memcmp(current_token(parser)->value, "attribute", 9) == 0)) &&
                 next && (next->type == TOK_NCNAME ||
                          next->type == TOK_QNAME) &&
                 parser->token_pos + 2 < parser->token_count &&
                 parser->tokens[parser->token_pos + 2].type == TOK_LBRACE) {
            int is_attr =
                current_token(parser)->value_len == 9;
            char name[128];
            size_t nl = next->value_len;
            if (nl >= sizeof(name)) return NULL;
            memcpy(name, next->value, nl);
            name[nl] = 0;
            advance_token(parser);
            advance_token(parser);
            advance_token(parser);
            XPathASTNode* ctor = ast_node_new(XPATH_AST_OPERATOR);
            if (!ctor) return NULL;
            ctor->number_value = (double)(is_attr ? XPATH_OP_ATTRIBUTE_CTOR
                                                  : XPATH_OP_ELEMENT_CTOR);
            ctor->value = leptris_strdup(name);
            /* Comma-separated ctor children attach directly (the
             * evaluator sees attribute and content children up
             * front). */
            for (;;) {
                XPathASTNode* item = parse_expr(parser);
                if (!item) { ast_node_free(ctor); return NULL; }
                ast_node_add_child(ctor, item);
                if (current_token_is(parser, TOK_COMMA)) {
                    advance_token(parser);
                    continue;
                }
                break;
            }
            if (!current_token_is(parser, TOK_RBRACE)) {
                ast_node_free(ctor);
                return NULL;
            }
            advance_token(parser);
            return parse_postfix_ops(parser, ctor);
        }
        /* If followed by '(', it's a function call - fall through to filter expression */
        else if (next && next->type == TOK_LPAREN) {
            /* Fall through to filter expression */
        }
        /* Otherwise (including operators, path operators, or EOF/end markers), it's a location path */
        else {
            return parse_location_path(parser);
        }
    }

    /* Try filter expression */
    XPathASTNode* expr = parse_filter_expr(parser);
    if (!expr) return NULL;

    /* Check for path continuation */
    if (current_token_is(parser, TOK_SLASH) || current_token_is(parser, TOK_DOUBLE_SLASH)) {
        XPathASTNode* path = ast_node_new(XPATH_AST_PATH_EXPR);
        if (!path) {
            ast_node_free(expr);
            return NULL;
        }

        ast_node_add_child(path, expr);

        int is_double = current_token_is(parser, TOK_DOUBLE_SLASH);
        advance_token(parser);

        XPathASTNode* rel_path = parse_relative_location_path(parser);
        if (!rel_path) {
            ast_node_free(path);
            return NULL;
        }

        if (is_double) {
            XPathASTNode* desc_step = ast_node_new(XPATH_AST_STEP);
            if (desc_step) {
                desc_step->value = leptris_strdup("descendant-or-self");
                desc_step->axis_id = XPATH_AXIS_DESCENDANT_OR_SELF;
                XPathASTNode* node_test = ast_node_new(XPATH_AST_NODE_TEST_TYPE);
                if (node_test) {
                    node_test->value = leptris_strdup("node");
                }
                ast_node_add_child(desc_step, node_test);
                ast_node_add_child(path, desc_step);
            }
        }

        ast_node_add_child(path, rel_path);
        return path;
    }

    return expr;
}

/* Parse filter expressions */
static XPathASTNode* parse_filter_expr(XPathParser* parser);

/* Postfix operations on a primary: predicates + 3.1 lookups.
 * Shared by parse_filter_expr and the map/array constructors
 * (which return from the path dispatch before the filter level —
 * `map { 'b': 1 }?b` must still bind the lookup). */
static XPathASTNode* parse_postfix_ops(XPathParser* parser,
                                       XPathASTNode* expr) {
    while (current_token_is(parser, TOK_LBRACKET) ||
           current_token_is(parser, TOK_QUESTION) ||
           current_token_is(parser, TOK_LPAREN)) {
        /* 3.0 dynamic call: primary '(' args ')' — a '(' after a
         * complete primary can only be this (1.0 function calls
         * are primaries themselves). */
        if (current_token_is(parser, TOK_LPAREN)) {
            advance_token(parser);
            XPathASTNode* call = ast_node_new(XPATH_AST_OPERATOR);
            if (!call) { ast_node_free(expr); return NULL; }
            call->number_value = (double)XPATH_OP_DYN_CALL;
            ast_node_add_child(call, expr);
            while (!current_token_is(parser, TOK_RPAREN)) {
                XPathASTNode* a = parse_expr(parser);
                if (!a) { ast_node_free(call); return NULL; }
                ast_node_add_child(call, a);
                if (current_token_is(parser, TOK_COMMA))
                    advance_token(parser);
                else break;
            }
            if (!current_token_is(parser, TOK_RPAREN)) {
                ast_node_free(call);
                return NULL;
            }
            advance_token(parser);
            expr = call;
            continue;
        }
        if (current_token_is(parser, TOK_QUESTION)) {
            advance_token(parser);
            XPathToken* kt = current_token(parser);
            if (!kt || (kt->type != TOK_NCNAME && kt->type != TOK_QNAME &&
                        kt->type != TOK_NUMBER)) {
                ast_node_free(expr);
                return NULL;
            }
            char key[96];
            size_t klen = kt->value_len < sizeof(key) - 1
                              ? kt->value_len : sizeof(key) - 1;
            memcpy(key, kt->value, klen);
            key[klen] = '\0';
            advance_token(parser);
            XPathASTNode* lk = ast_node_new(XPATH_AST_OPERATOR);
            if (!lk) { ast_node_free(expr); return NULL; }
            lk->number_value = (double)XPATH_OP_LOOKUP;
            lk->value = leptris_strdup(key);
            ast_node_add_child(lk, expr);
            expr = lk;
            continue;
        }
        XPathASTNode* pred = parse_predicate(parser);
        if (!pred) {
            ast_node_free(expr);
            return NULL;
        }

        XPathASTNode* filter = ast_node_new(XPATH_AST_PREDICATE);
        if (!filter) {
            ast_node_free(expr);
            ast_node_free(pred);
            return NULL;
        }

        ast_node_add_child(filter, expr);
        ast_node_add_child(filter, pred);
        expr = filter;
    }
    return expr;
}

static XPathASTNode* parse_filter_expr(XPathParser* parser) {
    XPathASTNode* expr = parse_primary_expr(parser);
    if (!expr) return NULL;
    return parse_postfix_ops(parser, expr);
}

/* Parse primary expressions: NUMBER | STRING | FunctionCall | '(' Expr ')' */
/* XPath 2.0+ conditional (XSLT 3.0 expressions):
 * `if (cond) then expr else expr`. `then`/`else` are matched by
 * token VALUE at the expected position — they stay NCNames
 * everywhere else (name tests, paths) keep parsing. */
static XPathASTNode* parse_if_expr(XPathParser* parser) {
    /* current token is `if`; the ( follows. */
    advance_token(parser);   /* consume `if` */
    XPathToken* lp = current_token(parser);
    if (!lp || lp->type != TOK_LPAREN) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected '(' after if");
        return NULL;
    }
    advance_token(parser);

    XPathASTNode* cond = parse_expr(parser);
    if (!cond) return NULL;

    /* The parens wrap ONLY the condition:
     * if ( cond ) then expr else expr. */
    XPathToken* rp0 = current_token(parser);
    if (!rp0 || rp0->type != TOK_RPAREN) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected ')' after if condition");
        ast_node_free(cond);
        return NULL;
    }
    advance_token(parser);

    XPathToken* t = current_token(parser);
    if (!t || t->type != TOK_NCNAME || t->value_len != 4 ||
        memcmp(t->value, "then", 4) != 0) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected 'then' in if expression");
        ast_node_free(cond);
        return NULL;
    }
    advance_token(parser);

    XPathASTNode* then_e = parse_expr(parser);
    if (!then_e) { ast_node_free(cond); return NULL; }

    t = current_token(parser);
    if (!t || t->type != TOK_NCNAME || t->value_len != 4 ||
        memcmp(t->value, "else", 4) != 0) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected 'else' in if expression");
        ast_node_free(cond);
        ast_node_free(then_e);
        return NULL;
    }
    advance_token(parser);

    XPathASTNode* else_e = parse_expr(parser);
    if (!else_e) {
        ast_node_free(cond);
        ast_node_free(then_e);
        return NULL;
    }

    XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
    if (!node) {
        ast_node_free(cond);
        ast_node_free(then_e);
        ast_node_free(else_e);
        return NULL;
    }
    node->number_value = (double)XPATH_OP_IF;
    ast_node_add_child(node, cond);
    ast_node_add_child(node, then_e);
    ast_node_add_child(node, else_e);
    return node;
}

/* XPath 2.0+ `for $v1 in E1, $v2 in E2 ... return R` (XSLT 3.0).
 * Single-variable form first; the operator node carries [0]=the
 * binding (an XPATH_AST_ARGUMENT-shaped pair), [1]=return expr. */
static XPathASTNode* parse_for_expr(XPathParser* parser) {
    advance_token(parser);   /* consume `for` */

    XPathToken* d = current_token(parser);
    if (!d || d->type != TOK_DOLLAR) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected '$' variable after for");
        return NULL;
    }
    advance_token(parser);
    XPathToken* vt = current_token(parser);
    if (!vt || (vt->type != TOK_NCNAME && vt->type != TOK_QNAME)) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected variable name in for");
        return NULL;
    }
    char* var_name = token_to_string(vt);
    if (!var_name) return NULL;
    advance_token(parser);

    /* XQuery `for $x at $p` — the position name rides ->value as
     * var '\x01' pos (the evaluator splits it). */
    char* pos_name = NULL;
    {
        XPathToken* at = current_token(parser);
        if (at && at->type == TOK_NCNAME && at->value_len == 2 &&
            memcmp(at->value, "at", 2) == 0) {
            advance_token(parser);
            XPathToken* pd = current_token(parser);
            if (!pd || pd->type != TOK_DOLLAR) {
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                         "Expected '$' position variable after at");
                free(var_name);
                return NULL;
            }
            advance_token(parser);
            XPathToken* pt = current_token(parser);
            if (!pt || (pt->type != TOK_NCNAME && pt->type != TOK_QNAME)) {
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                         "Expected position variable name");
                free(var_name);
                return NULL;
            }
            pos_name = token_to_string(pt);
            if (!pos_name) { free(var_name); return NULL; }
            advance_token(parser);
        }
    }

    XPathToken* it = current_token(parser);
    if (!it || it->type != TOK_NCNAME || it->value_len != 2 ||
        memcmp(it->value, "in", 2) != 0) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected 'in' in for expression");
        free(var_name);
        free(pos_name);
        return NULL;
    }
    advance_token(parser);

    XPathASTNode* domain = parse_expr(parser);
    if (!domain) { free(var_name); free(pos_name); return NULL; }

    /* XQuery `where` — desugars to if (W, R, ()). */
    XPathASTNode* where_ast = NULL;
    {
        XPathToken* wt = current_token(parser);
        if (wt && wt->type == TOK_NCNAME && wt->value_len == 5 &&
            memcmp(wt->value, "where", 5) == 0) {
            advance_token(parser);
            where_ast = parse_expr(parser);
            if (!where_ast) {
                ast_node_free(domain);
                free(var_name);
                free(pos_name);
                return NULL;
            }
        }
    }

    XPathToken* rt = current_token(parser);
    if (!rt || rt->type != TOK_NCNAME || rt->value_len != 6 ||
        memcmp(rt->value, "return", 6) != 0) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected 'return' in for expression");
        ast_node_free(domain);
        ast_node_free(where_ast);
        free(var_name);
        free(pos_name);
        return NULL;
    }
    advance_token(parser);

    XPathASTNode* ret = parse_expr(parser);
    if (!ret) {
        ast_node_free(domain);
        ast_node_free(where_ast);
        free(var_name);
        free(pos_name);
        return NULL;
    }

    if (where_ast) {
        /* if (W, R, ()) — the empty sequence is a zero-child
         * SEQUENCE. */
        XPathASTNode* guard = ast_node_new(XPATH_AST_OPERATOR);
        XPathASTNode* empty = ast_node_new(XPATH_AST_OPERATOR);
        if (!guard || !empty) {
            ast_node_free(guard);
            ast_node_free(empty);
            ast_node_free(domain);
            ast_node_free(where_ast);
            ast_node_free(ret);
            free(var_name);
            free(pos_name);
            return NULL;
        }
        guard->number_value = (double)XPATH_OP_IF;
        empty->number_value = (double)XPATH_OP_SEQUENCE;
        ast_node_add_child(guard, where_ast);
        ast_node_add_child(guard, ret);
        ast_node_add_child(guard, empty);
        ret = guard;
    }

    XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
    if (!node) {
        ast_node_free(domain);
        ast_node_free(ret);
        free(var_name);
        free(pos_name);
        return NULL;
    }
    node->number_value = (double)XPATH_OP_FOR;
    if (pos_name) {
        size_t vl = strlen(var_name), pl = strlen(pos_name);
        char* joined = (char*)malloc(vl + pl + 2);
        if (!joined) {
            ast_node_free(domain);
            ast_node_free(ret);
            free(var_name);
            free(pos_name);
            return NULL;
        }
        memcpy(joined, var_name, vl);
        joined[vl] = '\x01';
        memcpy(joined + vl + 1, pos_name, pl + 1);
        node->value = joined;
        free(var_name);
        free(pos_name);
    } else {
        node->value = var_name;   /* the loop variable name */
        free(pos_name);
    }
    ast_node_add_child(node, domain);
    ast_node_add_child(node, ret);
    return node;
}

/* XPath 3.1 `let $x := E1, $y := E2 ... return B`. Bindings are
 * comma-separated (a top-level comma never belongs to the binding
 * expression — sequences are parenthesized); each binding may
 * reference the earlier ones. The operator node carries [0..n-1]
 * = binding values, [n] = body; ->value space-joins the names. */
static XPathASTNode* parse_let_expr(XPathParser* parser) {
    advance_token(parser);   /* consume `let` */

    XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
    if (!node) return NULL;
    node->number_value = (double)XPATH_OP_LET;

    char* names = NULL;
    size_t names_len = 0, names_cap = 0;

    for (;;) {
        XPathToken* d = current_token(parser);
        if (!d || d->type != TOK_DOLLAR) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected '$' variable in let binding");
            goto fail;
        }
        advance_token(parser);
        XPathToken* vt = current_token(parser);
        if (!vt || (vt->type != TOK_NCNAME && vt->type != TOK_QNAME)) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected variable name in let");
            goto fail;
        }
        char* var_name = token_to_string(vt);
        if (!var_name) goto fail;
        advance_token(parser);

        if (!consume_token(parser, TOK_ASSIGN,
                           "Expected ':=' in let binding")) {
            free(var_name);
            goto fail;
        }

        XPathASTNode* val = parse_expr(parser);
        if (!val) { free(var_name); goto fail; }

        /* Space-join the binding names (names contain no spaces). */
        size_t need = names_len + strlen(var_name) + 2;
        if (need > names_cap) {
            size_t cap = names_cap ? names_cap * 2 : 32;
            while (cap < need) cap *= 2;
            char* grown = (char*)realloc(names, cap);
            if (!grown) { free(var_name); ast_node_free(val); goto fail; }
            names = grown;
            names_cap = cap;
        }
        if (names_len) names[names_len++] = ' ';
        size_t vl = strlen(var_name);
        memcpy(names + names_len, var_name, vl);
        names_len += vl;
        names[names_len] = '\0';
        free(var_name);

        ast_node_add_child(node, val);

        if (current_token_is(parser, TOK_COMMA)) {
            advance_token(parser);
            continue;
        }
        break;
    }

    XPathToken* rt = current_token(parser);
    if (!rt || rt->type != TOK_NCNAME || rt->value_len != 6 ||
        memcmp(rt->value, "return", 6) != 0) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected 'return' in let expression");
        goto fail;
    }
    advance_token(parser);

    XPathASTNode* body = parse_expr(parser);
    if (!body) goto fail;
    ast_node_add_child(node, body);
    node->value = names;
    return node;

fail:
    free(names);
    ast_node_free(node);
    return NULL;
}

/* XPath 3.1 switch: `switch (E) { case T return R ... default
 * return D }`. Value-matched keywords keep name tests intact. */
#if 0
static XPathASTNode* parse_switch_expr(XPathParser* parser) {
    advance_token(parser);   /* consume `switch` */
    if (!consume_token(parser, TOK_LPAREN,
                       "Expected '(' after switch"))
        return NULL;
    XPathASTNode* operand = parse_expr(parser);
    if (!operand) return NULL;
    if (!consume_token(parser, TOK_RPAREN,
                       "Expected ')' after switch operand")) {
        ast_node_free(operand);
        return NULL;
    }
    if (!consume_token(parser, TOK_LBRACE,
                       "Expected '{' to open the switch body")) {
        ast_node_free(operand);
        return NULL;
    }
    XPathASTNode* node = ast_node_new(XPATH_AST_OPERATOR);
    if (!node) { ast_node_free(operand); return NULL; }
    node->number_value = (double)XPATH_OP_SWITCH;
    ast_node_add_child(node, operand);

    int have_default = 0;
    for (;;) {
        XPathToken* kw = current_token(parser);
        if (!kw || kw->type != TOK_NCNAME) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected 'case' or 'default' in switch body");
            ast_node_free(node);
            return NULL;
        }
        int is_default = kw->value_len == 7 &&
                         memcmp(kw->value, "default", 7) == 0;
        int is_case = kw->value_len == 4 &&
                      memcmp(kw->value, "case", 4) == 0;
        if (!is_default && !is_case) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected 'case' or 'default' in switch body");
            ast_node_free(node);
            return NULL;
        }
        advance_token(parser);
        if (is_case) {
            XPathASTNode* test = parse_expr(parser);
            if (!test) { ast_node_free(node); return NULL; }
            XPathToken* rt = current_token(parser);
            if (!rt || rt->type != TOK_NCNAME || rt->value_len != 6 ||
                memcmp(rt->value, "return", 6) != 0) {
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                         "Expected 'return' after switch case");
                ast_node_free(test);
                ast_node_free(node);
                return NULL;
            }
            advance_token(parser);
            XPathASTNode* res = parse_expr(parser);
            if (!res) { ast_node_free(node); return NULL; }
            ast_node_add_child(node, test);
            ast_node_add_child(node, res);
        } else {
            if (have_default) {
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                         "Duplicate default clause in switch");
                ast_node_free(node);
                return NULL;
            }
            have_default = 1;
            XPathToken* rt = current_token(parser);
            if (!rt || rt->type != TOK_NCNAME || rt->value_len != 6 ||
                memcmp(rt->value, "return", 6) != 0) {
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                         "Expected 'return' after switch default");
                ast_node_free(node);
                return NULL;
            }
            advance_token(parser);
            XPathASTNode* res = parse_expr(parser);
            if (!res) { ast_node_free(node); return NULL; }
            node->value = leptris_strdup("__switch_default");
            node->number_value = (double)XPATH_OP_SWITCH;
            /* default result rides as the LAST child; marked by the
             * node->value sentinel. */
            ast_node_add_child(node, res);
            break;
        }
        if (current_token_is(parser, TOK_RBRACE)) break;
    }
    if (!consume_token(parser, TOK_RBRACE,
                       "Expected '}' to close the switch body")) {
        ast_node_free(node);
        return NULL;
    }
    return node;
}
#endif


static XPathASTNode* parse_primary_expr(XPathParser* parser) {
    XPathToken* tok = current_token(parser);
    if (!tok) {
        if (parser->lexer && parser->lexer->input) {
            size_t byte_offset = (parser->lexer->end && parser->lexer->end >= parser->lexer->input)
                ? parser->lexer->end - parser->lexer->input
                : 0;
            leptris_set_error_with_context(
                LEPTRIS_ERROR_XPATH_SYNTAX,
                "Unexpected end of XPath expression",
                parser->lexer->input,
                byte_offset,
                parser->lexer->line,
                parser->lexer->column
            );
        }

        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Unexpected EOF in primary expression");
        return NULL;
    }

    /* Number literal */
    if (tok->type == TOK_NUMBER) {
        XPathASTNode* node = ast_node_new(XPATH_AST_NUMBER);
        if (!node) return NULL;

        char* num_str = token_to_string(tok);
        if (num_str) {
            node->number_value = strtod(num_str, NULL);
            LEPTRIS_FREE(num_str);
        }
        advance_token(parser);
        return node;
    }

    /* String literal */
    if (tok->type == TOK_STRING) {
        XPathASTNode* node = ast_node_new(XPATH_AST_STRING);
        if (!node) return NULL;

        /* Remove quotes */
        if (tok->value_len >= 2) {
            size_t len = tok->value_len - 2;
            node->value = LEPTRIS_ALLOC_N(char, len + 1);
            if (node->value) {
                memcpy(node->value, tok->value + 1, len);
                node->value[len] = '\0';
            }
        }
        advance_token(parser);
        return node;
    }

    /* Parenthesized expression */
    if (tok->type == TOK_LPAREN) {
        advance_token(parser);
        XPathASTNode* expr = parse_expr(parser);
        if (!expr) return NULL;

        /* XPath 2.0+ item sequence `('a','b',expr)`: comma after
         * the first member (XSLT 3.0). */
        if (current_token_is(parser, TOK_COMMA)) {
            XPathASTNode* seq = ast_node_new(XPATH_AST_OPERATOR);
            if (!seq) { ast_node_free(expr); return NULL; }
            seq->number_value = (double)XPATH_OP_SEQUENCE;
            ast_node_add_child(seq, expr);
            while (current_token_is(parser, TOK_COMMA)) {
                advance_token(parser);
                XPathASTNode* member = parse_expr(parser);
                if (!member) {
                    ast_node_free(seq);
                    return NULL;
                }
                ast_node_add_child(seq, member);
            }
            if (!consume_token(parser, TOK_RPAREN,
                               "Expected ')' after sequence")) {
                ast_node_free(seq);
                return NULL;
            }
            return seq;
        }

        if (!consume_token(parser, TOK_RPAREN, "Expected ')' after expression")) {
            ast_node_free(expr);
            return NULL;
        }
        return expr;
    }

    /* Node-type tokens are node tests, not functions: XPath 1.0
     * defines no text()/node()/comment()/processing-instruction()
     * function. `text()` in expression position (e.g. inside a
     * predicate, a[text()]) is a relative location path — a single
     * child-axis step with a type node test. */
    if (tok->type >= TOK_COMMENT && tok->type <= TOK_NODE) {
        XPathToken* next = peek_token(parser, 1);

        if (next && next->type == TOK_LPAREN) {
            XPathASTNode* node = ast_node_new(XPATH_AST_STEP);
            if (!node) return NULL;
            node->value = leptris_strdup("child");
            node->axis_id = XPATH_AXIS_CHILD;

            XPathASTNode* test = parse_node_test(parser);
            if (!test) {
                ast_node_free(node);
                return NULL;
            }
            ast_node_add_child(node, test);
            return node;
        }
    }

    /* `for $v in EXPR return EXPR` — token after `for` is `$`;
     * value-matched keyword keeps name tests intact. */
    if (tok->type == TOK_NCNAME && tok->value_len == 3 &&
        memcmp(tok->value, "for", 3) == 0) {
        XPathToken* fn = peek_token(parser, 1);
        if (fn && fn->type == TOK_DOLLAR)
            return parse_for_expr(parser);
    }

    /* XPath 3.1 `let $x := E return B` — same $-lookahead guard. */
    if (tok->type == TOK_NCNAME && tok->value_len == 3 &&
        memcmp(tok->value, "let", 3) == 0) {
        XPathToken* fn = peek_token(parser, 1);
        if (fn && fn->type == TOK_DOLLAR)
            return parse_let_expr(parser);
    }

    /* Function call with NCName/QName */
    if (tok->type == TOK_NCNAME || tok->type == TOK_QNAME) {
        XPathToken name_token = *tok;
        XPathToken* next = peek_token(parser, 1);

        if (next && next->type == TOK_LPAREN) {
            /* XPath 2.0+ `if (cond) then A else B`: `if` followed by
             * `(` is the conditional form — XSLT 3.0 expressions.
             * `then`/`else` are matched by VALUE at the expected
             * position so NCName name tests keep working. */
            if (tok->type == TOK_NCNAME && tok->value_len == 2 &&
                memcmp(tok->value, "if", 2) == 0) {
                return parse_if_expr(parser);
            }
            advance_token(parser);
            return parse_function_call(parser, name_token.value, name_token.value_len);
        }
    }

    /* Variable reference: $varname */
    if (tok->type == TOK_DOLLAR) {
        advance_token(parser);  /* Consume $ */

        /* Expect variable name (NCName) */
        XPathToken* name_tok = current_token(parser);
        if (!name_tok || (name_tok->type != TOK_NCNAME && name_tok->type != TOK_QNAME)) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                     "Expected variable name after $ at line %d, column %d",
                     tok->line, tok->column);
            return NULL;
        }

        XPathASTNode* node = ast_node_new(XPATH_AST_VARIABLE_REFERENCE);
        if (!node) return NULL;

        /* Store variable name */
        node->value = LEPTRIS_ALLOC_N(char, name_tok->value_len + 1);
        if (node->value) {
            memcpy(node->value, name_tok->value, name_tok->value_len);
            node->value[name_tok->value_len] = '\0';
        }

        advance_token(parser);  /* Consume variable name */
        return node;
    }

    if (parser->lexer && parser->lexer->input && tok->value) {
        size_t byte_offset = (tok->value >= parser->lexer->input)
            ? tok->value - parser->lexer->input
            : 0;
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Unexpected token in primary expression: %s",
                 xpath_token_type_to_string(tok->type));

        leptris_set_error_with_context(
            LEPTRIS_ERROR_XPATH_SYNTAX,
            msg,
            parser->lexer->input,
            byte_offset,
            tok->line,
            tok->column
        );
    }

    snprintf(parser->error_msg, sizeof(parser->error_msg),
             "Unexpected token %s at line %d, column %d in primary expression",
             xpath_token_type_to_string(tok->type),
             tok->line, tok->column);
    return NULL;
}

/* Parse function call */
static XPathASTNode* parse_function_call(XPathParser* parser, const char* name, size_t name_len) {
    XPathASTNode* node = ast_node_new(XPATH_AST_FUNCTION_CALL);
    if (!node) return NULL;

    node->value = LEPTRIS_ALLOC_N(char, name_len + 1);
    if (node->value) {
        memcpy(node->value, name, name_len);
        node->value[name_len] = '\0';
    }

    if (!consume_token(parser, TOK_LPAREN, "Expected '(' after function name")) {
        ast_node_free(node);
        return NULL;
    }

    /* Parse arguments. A leading `?` is a partial-application
     * placeholder: concat('x', ?, 'z') desugars to the inline fn
     * function($_1){ concat('x', $_1, 'z') } — the placeholder
     * becomes a variable reference bound at dynamic-call time. */
    size_t holes = 0;
    if (!current_token_is(parser, TOK_RPAREN)) {
        do {
            if (current_token_is(parser, TOK_QUESTION)) {
                advance_token(parser);
                /* Hole names cannot collide with user variables
                 * (NCNames exclude '%') and must not embed '\x01' —
                 * that byte separates the INLINE_FN params string. */
                char pname[16];
                snprintf(pname, sizeof(pname), "%%%zu", holes + 1);
                XPathASTNode* v =
                    ast_node_new(XPATH_AST_VARIABLE_REFERENCE);
                if (v) v->value = leptris_strdup(pname);
                if (!v || !v->value) {
                    ast_node_free(v);
                    ast_node_free(node);
                    return NULL;
                }
                ast_node_add_child(node, v);
                holes++;
                continue;
            }
            XPathASTNode* arg = parse_expr(parser);
            if (!arg) {
                ast_node_free(node);
                return NULL;
            }
            ast_node_add_child(node, arg);
        } while (match_token(parser, TOK_COMMA));
    }

    if (!consume_token(parser, TOK_RPAREN, "Expected ')' after function arguments")) {
        ast_node_free(node);
        return NULL;
    }

    if (!holes) return node;

    /* Re-wrap the call as an inline function over the hole params
     * (joined with '\x01', matching the INLINE_FN format). */
    char params[128];
    size_t plen = 0;
    for (size_t i = 1; i <= holes; i++) {
        int w = snprintf(params + plen, sizeof(params) - plen,
                         i > 1 ? "\x01%%%zu" : "%%%zu", i);
        if (w < 0 || (size_t)w >= sizeof(params) - plen) {
            ast_node_free(node);
            return NULL;
        }
        plen += (size_t)w;
    }
    XPathASTNode* fn = ast_node_new(XPATH_AST_OPERATOR);
    if (!fn) { ast_node_free(node); return NULL; }
    fn->number_value = (double)XPATH_OP_INLINE_FN;
    fn->value = leptris_strdup(params);
    if (!fn->value) { ast_node_free(fn); ast_node_free(node); return NULL; }
    ast_node_add_child(fn, node);
    return parse_postfix_ops(parser, fn);
}

/* ============================================================================
 * Location Path Parsers
 * ============================================================================ */

/* Parse location path */
static XPathASTNode* parse_location_path(XPathParser* parser) {
    XPathASTNode* node;

    /* Absolute path starting with / */
    if (current_token_is(parser, TOK_SLASH)) {
        advance_token(parser);

        node = ast_node_new(XPATH_AST_ABSOLUTE_PATH);
        if (!node) return NULL;

        /* If followed by a step, parse relative path */
        if (!current_token_is(parser, TOK_EOF) &&
            !current_token_is(parser, TOK_RPAREN) &&
            !current_token_is(parser, TOK_RBRACKET) &&
            !current_token_is(parser, TOK_PIPE)) {

            XPathASTNode* rel = parse_relative_location_path(parser);
            if (!rel) {
                ast_node_free(node);
                return NULL;
            }
            ast_node_add_child(node, rel);
        }

        return node;
    }

    /* Absolute path starting with // */
    if (current_token_is(parser, TOK_DOUBLE_SLASH)) {
        advance_token(parser);

        node = ast_node_new(XPATH_AST_ABSOLUTE_PATH);
        if (!node) return NULL;

        /* Optimization: double-slash followed by star should be a single descendant-or-self::* step,
         * not two steps (descendant-or-self::node() + child::*).
         * This matches how most XPath implementations handle double-slash-star for efficiency. */
        if (current_token_is(parser, TOK_STAR)) {
            /* Create single step: descendant-or-self::* */
            XPathASTNode* desc_step = ast_node_new(XPATH_AST_STEP);
            if (!desc_step) {
                ast_node_free(node);
                return NULL;
            }
            desc_step->value = leptris_strdup("descendant-or-self");
            desc_step->axis_id = XPATH_AXIS_DESCENDANT_OR_SELF;

            /* Use wildcard node test instead of node() */
            XPathASTNode* node_test = ast_node_new(XPATH_AST_NODE_TEST_ALL);
            if (!node_test) {
                ast_node_free(desc_step);
                ast_node_free(node);
                return NULL;
            }
            ast_node_add_child(desc_step, node_test);

            /* Consume the * token */
            advance_token(parser);

            /* Parse predicates (FIX: was missing before!) */
            while (current_token_is(parser, TOK_LBRACKET)) {
                XPathASTNode* pred = parse_predicate(parser);
                if (!pred) {
                    ast_node_free(desc_step);
                    ast_node_free(node);
                    return NULL;
                }
                ast_node_add_child(desc_step, pred);
            }

            ast_node_add_child(node, desc_step);

            return node;
        }

        /* General case: Add descendant-or-self::node() step */
        XPathASTNode* desc_step = ast_node_new(XPATH_AST_STEP);
        if (!desc_step) {
            ast_node_free(node);
            return NULL;
        }

        desc_step->value = leptris_strdup("descendant-or-self");
        desc_step->axis_id = XPATH_AXIS_DESCENDANT_OR_SELF;
        XPathASTNode* node_test = ast_node_new(XPATH_AST_NODE_TEST_TYPE);
        if (!node_test) {
            ast_node_free(desc_step);
            ast_node_free(node);
            return NULL;
        }
        node_test->value = leptris_strdup("node");
        ast_node_add_child(desc_step, node_test);
        ast_node_add_child(node, desc_step);

        /* Parse relative path */
        XPathASTNode* rel = parse_relative_location_path(parser);
        if (!rel) {
            ast_node_free(node);
            return NULL;
        }
        ast_node_add_child(node, rel);

        return node;
    }

    /* Relative path */
    XPathASTNode* rel = parse_relative_location_path(parser);
    if (!rel) return NULL;

    /* Unwrap single-step relative paths */
    if (rel->type == XPATH_AST_RELATIVE_PATH && rel->child_count == 1) {
        XPathASTNode* single_step = rel->children[0];
        rel->children[0] = NULL;
        rel->child_count = 0;
        ast_node_free(rel);
        return single_step;
    }

    return rel;
}

/* Parse relative location path */
static XPathASTNode* parse_relative_location_path(XPathParser* parser) {
    XPathASTNode* node = ast_node_new(XPATH_AST_RELATIVE_PATH);
    if (!node) return NULL;

    /* Parse first step */
    XPathASTNode* step = parse_step(parser);
    if (!step) {
        ast_node_free(node);
        return NULL;
    }
    ast_node_add_child(node, step);

    /* Parse additional steps */
    while (current_token_is(parser, TOK_SLASH) || current_token_is(parser, TOK_DOUBLE_SLASH)) {
        int is_double = current_token_is(parser, TOK_DOUBLE_SLASH);
        advance_token(parser);

        if (is_double) {
            /* Insert descendant-or-self::node() step */
            XPathASTNode* desc_step = ast_node_new(XPATH_AST_STEP);
            if (!desc_step) {
                ast_node_free(node);
                return NULL;
            }
            desc_step->value = leptris_strdup("descendant-or-self");
            desc_step->axis_id = XPATH_AXIS_DESCENDANT_OR_SELF;
            XPathASTNode* node_test = ast_node_new(XPATH_AST_NODE_TEST_TYPE);
            if (node_test) {
                node_test->value = leptris_strdup("node");
            }
            ast_node_add_child(desc_step, node_test);
            ast_node_add_child(node, desc_step);
        }

        step = parse_step(parser);
        if (!step) {
            ast_node_free(node);
            return NULL;
        }
        ast_node_add_child(node, step);
    }

    return node;
}

/* Parse step */
static XPathASTNode* parse_step(XPathParser* parser) {
    /* Handle abbreviated steps */
    if (current_token_is(parser, TOK_DOT)) {
        advance_token(parser);
        XPathASTNode* node = ast_node_new(XPATH_AST_STEP);
        if (node) {
            node->value = leptris_strdup("self");
            node->axis_id = XPATH_AXIS_SELF;
            /* "." is self::node() — a KIND test, not "*": it must
             * match any node kind (text/comment/PI/namespace), while
             * the name wildcard stays elements-only. */
            XPathASTNode* test = ast_node_new(XPATH_AST_NODE_TEST_TYPE);
            if (test) test->value = leptris_strdup("node");
            ast_node_add_child(node, test);
        }
        return node;
    }

    if (current_token_is(parser, TOK_DOUBLE_DOT)) {
        advance_token(parser);
        XPathASTNode* node = ast_node_new(XPATH_AST_STEP);
        if (node) {
            node->value = leptris_strdup("parent");
            node->axis_id = XPATH_AXIS_PARENT;
            XPathASTNode* test = ast_node_new(XPATH_AST_NODE_TEST_ALL);
            ast_node_add_child(node, test);
        }
        return node;
    }

    /* Handle @ abbreviation */
    if (current_token_is(parser, TOK_AT)) {
        advance_token(parser);
        XPathASTNode* node = ast_node_new(XPATH_AST_STEP);
        if (!node) return NULL;

        node->value = leptris_strdup("attribute");
        node->axis_id = XPATH_AXIS_ATTRIBUTE;

        XPathASTNode* test = parse_node_test(parser);
        if (!test) {
            ast_node_free(node);
            return NULL;
        }
        ast_node_add_child(node, test);

        /* Parse predicates */
        while (current_token_is(parser, TOK_LBRACKET)) {
            XPathASTNode* pred = parse_predicate(parser);
            if (!pred) {
                ast_node_free(node);
                return NULL;
            }
            ast_node_add_child(node, pred);
        }

        return node;
    }

    XPathASTNode* node = ast_node_new(XPATH_AST_STEP);
    if (!node) return NULL;

    /* Check for axis specifier */
    char* axis = NULL;
    XPathToken* tok = current_token(parser);

    if (tok && tok->type >= TOK_ANCESTOR && tok->type <= TOK_SELF) {
        axis = token_to_string(tok);
        advance_token(parser);

        if (!consume_token(parser, TOK_DOUBLE_COLON, "Expected '::' after axis name")) {
            if (axis) LEPTRIS_FREE(axis);
            ast_node_free(node);
            return NULL;
        }
    }

    node->value = axis ? leptris_strdup(axis) : leptris_strdup("child");
    if (axis) LEPTRIS_FREE(axis);

    /* Pre-compute axis enum so apply_axis can dispatch via switch
     * instead of strcmp chain (TODO 113 Phase 1). */
    node->axis_id = xpath_axis_from_name(node->value);

    /* Parse node test */
    XPathASTNode* test = parse_node_test(parser);
    if (!test) {
        ast_node_free(node);
        return NULL;
    }
    ast_node_add_child(node, test);

    /* Parse predicates */
    while (current_token_is(parser, TOK_LBRACKET)) {
        XPathASTNode* pred = parse_predicate(parser);
        if (!pred) {
            ast_node_free(node);
            return NULL;
        }
        ast_node_add_child(node, pred);
    }

    return node;
}

/* ============================================================================
 * Node Test and Predicate Parsers
 * ============================================================================ */

/* Parse node test */
static XPathASTNode* parse_node_test(XPathParser* parser) {
    XPathToken* tok = current_token(parser);
    if (!tok) {
        if (parser->lexer && parser->lexer->input) {
            size_t byte_offset = (parser->lexer->end && parser->lexer->end >= parser->lexer->input)
                ? parser->lexer->end - parser->lexer->input
                : 0;
            leptris_set_error_with_context(
                LEPTRIS_ERROR_XPATH_SYNTAX,
                "Expected node test, got end of expression",
                parser->lexer->input,
                byte_offset,
                parser->lexer->line,
                parser->lexer->column
            );
        }

        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "Expected node test at EOF");
        return NULL;
    }

    /* Node type tests */
    if (tok->type == TOK_COMMENT || tok->type == TOK_TEXT ||
        tok->type == TOK_NODE || tok->type == TOK_PROCESSING_INSTRUCTION) {

        XPathASTNode* node = ast_node_new(XPATH_AST_NODE_TEST_TYPE);
        if (!node) return NULL;

        node->value = token_to_string(tok);
        advance_token(parser);

        if (!consume_token(parser, TOK_LPAREN, "Expected '(' after node type")) {
            ast_node_free(node);
            return NULL;
        }

        if (current_token_is(parser, TOK_STRING)) {
            /* PI node test target argument, e.g.,
             * processing-instruction('xml-stylesheet'). The lexer
             * includes the surrounding quotes in token->value; strip
             * them so local_name holds the bare target name. */
            const XPathToken* lit_tok = current_token(parser);
            if (lit_tok->value_len >= 2 &&
                (lit_tok->value[0] == '\'' || lit_tok->value[0] == '"') &&
                lit_tok->value[0] == lit_tok->value[lit_tok->value_len - 1]) {
                size_t inner_len = lit_tok->value_len - 2;
                char* arg = LEPTRIS_ALLOC_N(char, inner_len + 1);
                if (arg) {
                    memcpy(arg, lit_tok->value + 1, inner_len);
                    arg[inner_len] = '\0';
                    node->local_name = arg;
                }
            } else {
                node->local_name = token_to_string(lit_tok);
            }
            advance_token(parser);
        }

        if (!consume_token(parser, TOK_RPAREN, "Expected ')' after node type")) {
            ast_node_free(node);
            return NULL;
        }

        return node;
    }

    /* Check for namespace wildcard: prefix:*
     * Lexer produces: TOK_NCNAME("prefix") followed by TOK_STAR
     * when it sees prefix:* because * is not an ncname_start char */
    if (tok->type == TOK_NCNAME) {
        XPathToken* next = peek_token(parser, 1);

        /* Check if next token is * (namespace wildcard pattern) */
        if (next && next->type == TOK_STAR) {
            /* This is prefix:* pattern */
            XPathASTNode* node = ast_node_new(XPATH_AST_NODE_TEST_ALL);
            if (!node) return NULL;

            /* Set prefix from NCName token */
            node->prefix = token_to_string(tok);
            node->value = leptris_strdup("*");

            advance_token(parser);  /* Consume NCName */
            advance_token(parser);  /* Consume STAR */

            return node;
        }
        /* Otherwise fall through to normal name test handling below */
    }

    /* Wildcard */
    if (tok->type == TOK_STAR) {
        advance_token(parser);
        return ast_node_new(XPATH_AST_NODE_TEST_ALL);
    }

    /* Name test */
    if (tok->type == TOK_NCNAME || tok->type == TOK_QNAME) {
        XPathASTNode* node = ast_node_new(XPATH_AST_NODE_TEST_NAME);
        if (!node) return NULL;

        /* Get full QName string */
        char* full_name = token_to_string(tok);
        if (!full_name) {
            ast_node_free(node);
            return NULL;
        }

        /* Split into prefix and local name (v0.8.0 namespace support) */
        char* colon = strchr(full_name, ':');
        if (colon && tok->type == TOK_QNAME) {
            /* Has namespace prefix */
            size_t prefix_len = colon - full_name;
            node->prefix = LEPTRIS_ALLOC_N(char, prefix_len + 1);
            if (node->prefix) {
                memcpy(node->prefix, full_name, prefix_len);
                node->prefix[prefix_len] = '\0';
            }
            node->local_name = leptris_strdup(colon + 1);
        } else {
            /* No prefix - simple NCName */
            node->prefix = NULL;
            node->local_name = leptris_strdup(full_name);
        }

        node->value = full_name;  /* Keep full name for backward compat */
        advance_token(parser);

        return node;
    }

    /* v1.1.0: Allow operator keywords as element names in node tests
     * This fixes axis::name syntax where name happens to be a keyword
     * e.g., ancestor::div, child::mod, parent::and, self::or */
    if (tok->type == TOK_DIV || tok->type == TOK_MOD ||
        tok->type == TOK_AND || tok->type == TOK_OR) {
        XPathASTNode* node = ast_node_new(XPATH_AST_NODE_TEST_NAME);
        if (!node) return NULL;

        char* name = token_to_string(tok);
        if (!name) {
            ast_node_free(node);
            return NULL;
        }

        node->prefix = NULL;
        node->local_name = leptris_strdup(name);
        node->value = name;
        advance_token(parser);

        return node;
    }

    if (parser->lexer && parser->lexer->input && tok->value) {
        size_t byte_offset = (tok->value >= parser->lexer->input)
            ? tok->value - parser->lexer->input
            : 0;

        leptris_set_error_with_context(
            LEPTRIS_ERROR_XPATH_SYNTAX,
            "Expected node test",
            parser->lexer->input,
            byte_offset,
            tok->line,
            tok->column
        );
    }

    snprintf(parser->error_msg, sizeof(parser->error_msg),
             "Expected node test at line %d, column %d",
             tok->line, tok->column);
    return NULL;
}

/* Parse predicate */
static XPathASTNode* parse_predicate(XPathParser* parser) {
    if (!consume_token(parser, TOK_LBRACKET, "Expected '[' to start predicate")) {
        return NULL;
    }

    XPathASTNode* expr = parse_expr(parser);
    if (!expr) return NULL;

    if (!consume_token(parser, TOK_RBRACKET, "Expected ']' to end predicate")) {
        ast_node_free(expr);
        return NULL;
    }

    return expr;
}