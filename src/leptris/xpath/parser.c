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

/* Path parsers */
static XPathASTNode* parse_path_expr(XPathParser* parser);
static XPathASTNode* parse_filter_expr(XPathParser* parser);
static XPathASTNode* parse_primary_expr(XPathParser* parser);
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

static XPathASTNode* parse_expr(XPathParser* parser) {
    return parse_or_expr(parser);
}

XPathASTNode* xpath_parse(XPathParser* parser) {
    if (!parser) return NULL;

    XPathASTNode* ast = parse_expr(parser);

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
static XPathASTNode* parse_relational_expr(XPathParser* parser) {
    XPathASTNode* left = parse_additive_expr(parser);
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
        XPathASTNode* right = parse_additive_expr(parser);
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

    return parse_union_expr(parser);
}

/* Parse union expressions: PathExpr ( '|' PathExpr )* */
static XPathASTNode* parse_union_expr(XPathParser* parser) {
    XPathASTNode* left = parse_path_expr(parser);
    if (!left) return NULL;

    while (current_token_is(parser, TOK_PIPE)) {
        advance_token(parser);
        XPathASTNode* right = parse_path_expr(parser);
        if (!right) {
            ast_node_free(left);
            return NULL;
        }

        left = create_operator_node(XPATH_OP_UNION, left, right);
        if (!left) return NULL;
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

    /* Check for relative paths starting with NCName/QName */
    if (current_token_is(parser, TOK_NCNAME) || current_token_is(parser, TOK_QNAME)) {
        XPathToken* next = peek_token(parser, 1);

        /* If followed by '(', it's a function call - fall through to filter expression */
        if (next && next->type == TOK_LPAREN) {
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
static XPathASTNode* parse_filter_expr(XPathParser* parser) {
    XPathASTNode* expr = parse_primary_expr(parser);
    if (!expr) return NULL;

    /* Parse predicates */
    while (current_token_is(parser, TOK_LBRACKET)) {
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

/* Parse primary expressions: NUMBER | STRING | FunctionCall | '(' Expr ')' */
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

        if (!consume_token(parser, TOK_RPAREN, "Expected ')' after expression")) {
            ast_node_free(expr);
            return NULL;
        }
        return expr;
    }

    /* Function call with node type tokens */
    if (tok->type >= TOK_COMMENT && tok->type <= TOK_NODE) {
        XPathToken name_token = *tok;
        XPathToken* next = peek_token(parser, 1);

        if (next && next->type == TOK_LPAREN) {
            advance_token(parser);
            return parse_function_call(parser, name_token.value, name_token.value_len);
        }
    }

    /* Function call with NCName/QName */
    if (tok->type == TOK_NCNAME || tok->type == TOK_QNAME) {
        XPathToken name_token = *tok;
        XPathToken* next = peek_token(parser, 1);

        if (next && next->type == TOK_LPAREN) {
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

    /* Parse arguments */
    if (!current_token_is(parser, TOK_RPAREN)) {
        do {
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

    return node;
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
            XPathASTNode* test = ast_node_new(XPATH_AST_NODE_TEST_ALL);
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
            const XPathToken* tok = current_token(parser);
            if (tok->value_len >= 2 &&
                (tok->value[0] == '\'' || tok->value[0] == '"') &&
                tok->value[0] == tok->value[tok->value_len - 1]) {
                size_t inner_len = tok->value_len - 2;
                char* arg = LEPTRIS_ALLOC_N(char, inner_len + 1);
                if (arg) {
                    memcpy(arg, tok->value + 1, inner_len);
                    arg[inner_len] = '\0';
                    node->local_name = arg;
                }
            } else {
                node->local_name = token_to_string(tok);
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