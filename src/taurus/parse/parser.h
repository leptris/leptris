/* lib/src/parse/parser.h - Integrated XML Parser
 * Copyright (c) 2024, Ribose Inc.
 *
 * Direct character-level parser that creates DOM nodes immediately.
 * No intermediate lexer - parses XML constructs on demand.
 *
 * CRITICAL RULE: NEVER trim whitespace - preserve ALL characters exactly.
 */

#ifndef TAURUS_PARSER_H
#define TAURUS_PARSER_H

#include "../dom/element.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/doctype.h"
#include "../taurus_internal.h"

/* Parser state structure */
typedef struct {
    const char* input;      /* Original input string (never modified) */
    const char* pos;        /* Current read position */
    const char* end;        /* End of input (input + length) */
    int line;              /* Current line number (1-based) */
    int column;            /* Current column number (1-based) */
    char error[256];       /* Error message if parsing fails */
    int has_error;         /* 1 if error occurred, 0 otherwise */

    /* XML Declaration and BOM tracking */
    int has_bom;                 /* 1 if UTF-8 BOM was present */
    char* xml_version;           /* Extracted version or NULL */
    char* encoding;              /* Extracted encoding or NULL */
    int standalone;              /* -1=not set, 0=no, 1=yes */
    int had_declaration;         /* 1 if <?xml?> was present */

    /* DOCTYPE storage */
    TaurusDoctypeNode* doctype;  /* Parsed DOCTYPE node or NULL */
    void* dtd;                   /* Parsed DTD (TaurusDTD*) for entity resolution */

    /* Document-level Processing Instructions (for C14N) */
    struct taurus_processing_instruction* pi_list;  /* PIs before root element */
    struct taurus_processing_instruction* pi_list_tail;  /* Tail of PI list */

    /* Memory pool for fast DOM allocation */
    TaurusMemoryPool* pool;      /* Pool for allocating DOM nodes */

    /* In-place parsing mode (zero-copy optimization) */
    int writable;                /* 1 if input can be modified in-place, 0 otherwise */

    /* PERFORMANCE: Track if any namespace prefixes were found during parsing
     * This allows us to skip post-parse namespace resolution for documents without namespaces */
    int has_namespace_prefixes;  /* 1 if any element has a prefix (e.g., "foo:bar"), 0 otherwise */

    /* Per-document strict mode (thread-safe, no global state) */
    int strict_mode;             /* 1=strict XML 1.0, 0=lenient (pugixml compat) */

    /* PERFORMANCE: Skip namespace resolution for faster parsing
     * When set, namespaces are still parsed but URIs are not resolved */
    int skip_namespace_resolution;  /* 1=skip namespace resolution, 0=resolve (default) */
} Parser;

/* ============================================================================
 * Parser Lifecycle
 * ============================================================================ */

/* Create parser for given XML string with memory pool */
Parser* parser_create(const char* xml, size_t len, TaurusMemoryPool* pool);

/* Create parser with strict mode option */
Parser* parser_create_with_options(const char* xml, size_t len, TaurusMemoryPool* pool, int strict_mode);

/* Create parser with in-place optimization (writable buffer) */
Parser* parser_create_writable(char* xml, size_t len, TaurusMemoryPool* pool);

/* Create parser with in-place optimization and strict mode */
Parser* parser_create_writable_with_options(char* xml, size_t len, TaurusMemoryPool* pool, int strict_mode);

/* Create parser with full parse options (for taurus_parse_string_ex) */
Parser* parser_create_with_parse_options(const char* xml, size_t len, TaurusMemoryPool* pool,
                                         int strict_mode, int skip_namespace_resolution);

/* Free parser state */
void parser_free(Parser* p);

/* Check if parser has encountered an error */
int parser_has_error(Parser* p);

/* Getters for XML declaration info */
const char* parser_get_xml_version(Parser* p);
const char* parser_get_encoding(Parser* p);
int parser_get_standalone(Parser* p);
int parser_get_had_declaration(Parser* p);
int parser_get_has_bom(Parser* p);
TaurusDoctypeNode* parser_get_doctype(Parser* p);
TaurusDoctypeNode* parser_transfer_doctype(Parser* p);

/* Get document-level processing instructions */
struct taurus_processing_instruction* parser_get_pi_list(Parser* p);

/* ============================================================================
 * Main Parse Functions
 * ============================================================================ */

/* Parse entire document - returns root element */
TaurusElement parser_parse_document(Parser* p);

/* Parse next node from current position - dispatches to specific parsers */
TaurusNode* parser_parse_node(Parser* p);

/* Two-pass compact parser - creates 28-byte elements in single allocation
 * This is the FAST path for achieving 1.0x vs pugixml performance.
 * Use for read-heavy workloads where DOM modification is not needed.
 *
 * @param xml XML string
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return TaurusDocument or NULL on error
 */
struct taurus_document* taurus_parse_two_pass(const char* xml, size_t len, int* error_out);

/* ============================================================================
 * Specific Node Type Parsers
 * ============================================================================ */

/* Parse element: <name attrs>children</name> or <name attrs/> */
TaurusElement parser_parse_element(Parser* p);

/* Parse text content until '<' - NEVER trims whitespace! */
TaurusTextNode* parser_parse_text(Parser* p);

/* Parse comment: <!-- content --> */
TaurusCommentNode* parser_parse_comment(Parser* p);

/* Parse CDATA section: <![CDATA[ content ]]> */
TaurusCDATANode* parser_parse_cdata(Parser* p);

/* Parse processing instruction: <?target data?> */
TaurusPINode* parser_parse_pi(Parser* p);

/* Parse DOCTYPE declaration: <!DOCTYPE ...> */
TaurusDoctypeNode* parser_parse_doctype(Parser* p);

/* ============================================================================
 * Parser Utilities
 * ============================================================================ */

/* Skip whitespace (space, tab, newline, carriage return) */
void parser_skip_whitespace(Parser* p);

/* Peek at current character without advancing */
char parser_peek(Parser* p);

/* Peek at character N positions ahead */
char parser_peek_ahead(Parser* p, int offset);

/* Advance position and return current character */
char parser_advance(Parser* p);

/* Check if string matches at current position (case-sensitive) */
int parser_match(Parser* p, const char* str);

/* Check if at end of input */
int parser_at_end(Parser* p);

/* Set error message */
void parser_set_error(Parser* p, const char* message);

/* Check if character is whitespace */
int parser_is_whitespace(char c);

/* Check if character is name start character (letter, '_', ':') */
int parser_is_name_start(char c);

/* Check if character is name character (letter, digit, '.', '-', '_', ':') */
int parser_is_name_char(char c);

#endif /* TAURUS_PARSER_NEW_H */