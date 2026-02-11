/**
 * @file output.h
 * @brief Output formatting for Taurus CLI
 *
 * This file defines a pluggable output formatting system that supports
 * multiple output formats (XML, JSON, text) through a common interface.
 *
 * Design Principles:
 * - Open/Closed: Add new formats without modifying core
 * - Strategy Pattern: Formatters are interchangeable
 * - Type-Safe: Separate methods for each XPath result type
 * - Separation of Concerns: Formatting separate from business logic
 */

#ifndef TAURUS_CLI_OUTPUT_H
#define TAURUS_CLI_OUTPUT_H

#include <stdio.h>
#include <stdbool.h>
#include "../src/include/taurus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Output Format Types                                                       */
/* ------------------------------------------------------------------------- */

/**
 * Output format enumeration
 *
 * Determines how results are rendered to the user.
 */
typedef enum {
    OUTPUT_FORMAT_XML,      /**< XML format (default, xmllint-compatible) */
    OUTPUT_FORMAT_JSON,     /**< JSON format (for scripts) */
    OUTPUT_FORMAT_TEXT      /**< Plain text (for human reading) */
} output_format_t;

/**
 * Parse output format from string
 *
 * @param format_str Format string ("xml", "json", "text")
 * @return Output format or OUTPUT_FORMAT_XML if invalid
 */
output_format_t output_format_from_string(const char* format_str);

/**
 * Convert output format to string
 *
 * @param format Format to convert
 * @return String representation
 */
const char* output_format_to_string(output_format_t format);

/* ------------------------------------------------------------------------- */
/* Output Formatter Interface (Strategy Pattern)                            */
/* ------------------------------------------------------------------------- */

/**
 * Output formatter interface
 *
 * Each formatter implements this interface to provide format-specific
 * rendering of Taurus results.
 *
 * Example usage:
 *
 * ```c
 * output_formatter_t* fmt = output_formatter_create(OUTPUT_FORMAT_XML);
 * struct taurus_document* doc = taurus_parse(xml, len);
 *
 * fmt->print_document(doc, stdout, fmt->context);
 *
 * output_formatter_free(fmt);
 * ```
 */
typedef struct output_formatter {
    output_format_t type;       /**< Format type */
    void* context;              /**< Format-specific context */

    /**
     * Print entire document
     *
     * @param doc Document to print
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_document)(
        struct taurus_document* doc,
        FILE* out,
        void* ctx
    );

    /**
     * Print element tree
     *
     * @param elem Element to print
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_element)(
        TaurusElement elem,
        FILE* out,
        void* ctx
    );

    /**
     * Print XPath nodeset result
     *
     * @param result XPath result (must be nodeset type)
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_nodeset)(
        struct taurus_xpath_result* result,
        FILE* out,
        void* ctx
    );

    /**
     * Print XPath string result
     *
     * @param str String value
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_string)(
        const char* str,
        FILE* out,
        void* ctx
    );

    /**
     * Print XPath number result
     *
     * @param num Number value
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_number)(
        double num,
        FILE* out,
        void* ctx
    );

    /**
     * Print XPath boolean result
     *
     * @param value Boolean value (0 or 1)
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_boolean)(
        int value,
        FILE* out,
        void* ctx
    );

    /**
     * Print error message
     *
     * @param msg Error message
     * @param out Output stream (typically stderr)
     * @param ctx Format-specific context
     */
    void (*print_error)(
        const char* msg,
        FILE* out,
        void* ctx
    );

    /**
     * Print success message
     *
     * @param msg Success message
     * @param out Output stream
     * @param ctx Format-specific context
     */
    void (*print_success)(
        const char* msg,
        FILE* out,
        void* ctx
    );
} output_formatter_t;

/* ------------------------------------------------------------------------- */
/* Formatter Factory                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Create output formatter
 *
 * Factory function that creates the appropriate formatter based on type.
 *
 * @param type Format type
 * @return Formatter instance or NULL on allocation failure
 */
output_formatter_t* output_formatter_create(output_format_t type);

/**
 * Free output formatter
 *
 * Frees the formatter and its context.
 *
 * @param fmt Formatter to free
 */
void output_formatter_free(output_formatter_t* fmt);

/* ------------------------------------------------------------------------- */
/* Format-Specific Options                                                  */
/* ------------------------------------------------------------------------- */

/**
 * XML formatter options
 *
 * Options specific to XML output formatting.
 */
typedef struct xml_format_options {
    int indent;                 /**< Indentation size (spaces) */
    bool pretty_print;          /**< Enable pretty-printing */
    bool include_declaration;   /**< Include <?xml?> declaration */
    const char* encoding;       /**< Output encoding (default: UTF-8) */
} xml_format_options_t;

/**
 * JSON formatter options
 *
 * Options specific to JSON output formatting.
 */
typedef struct json_format_options {
    int indent;                 /**< Indentation size (spaces) */
    bool pretty_print;          /**< Enable pretty-printing */
    bool include_metadata;      /**< Include metadata (types, etc.) */
} json_format_options_t;

/**
 * Text formatter options
 *
 * Options specific to text output formatting.
 */
typedef struct text_format_options {
    bool show_line_numbers;     /**< Show line numbers */
    bool show_xpath_path;       /**< Show XPath to each element */
    int max_depth;              /**< Maximum tree depth to show */
} text_format_options_t;

/**
 * Set XML formatter options
 *
 * @param fmt Formatter (must be XML type)
 * @param options XML-specific options
 */
void output_formatter_set_xml_options(
    output_formatter_t* fmt,
    const xml_format_options_t* options
);

/**
 * Set JSON formatter options
 *
 * @param fmt Formatter (must be JSON type)
 * @param options JSON-specific options
 */
void output_formatter_set_json_options(
    output_formatter_t* fmt,
    const json_format_options_t* options
);

/**
 * Set text formatter options
 *
 * @param fmt Formatter (must be text type)
 * @param options Text-specific options
 */
void output_formatter_set_text_options(
    output_formatter_t* fmt,
    const text_format_options_t* options
);

/* ------------------------------------------------------------------------- */
/* Convenience Functions                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Print XPath result (any type)
 *
 * Convenience function that dispatches to the appropriate formatter method
 * based on the result type.
 *
 * @param result XPath result
 * @param out Output stream
 * @param fmt Formatter
 */
void output_print_xpath_result(
    struct taurus_xpath_result* result,
    FILE* out,
    output_formatter_t* fmt
);

/**
 * Print count of results
 *
 * Used for --count option in xpath command.
 *
 * @param count Number of results
 * @param out Output stream
 * @param fmt Formatter
 */
void output_print_count(
    size_t count,
    FILE* out,
    output_formatter_t* fmt
);

/* ------------------------------------------------------------------------- */
/* Color Output Support (Optional)                                          */
/* ------------------------------------------------------------------------- */

/**
 * Color codes for terminal output
 */
typedef enum {
    COLOR_NONE,         /**< No color */
    COLOR_RED,          /**< Red (errors) */
    COLOR_GREEN,        /**< Green (success) */
    COLOR_YELLOW,       /**< Yellow (warnings) */
    COLOR_BLUE,         /**< Blue (info) */
    COLOR_CYAN,         /**< Cyan (highlights) */
    COLOR_MAGENTA       /**< Magenta (special) */
} color_code_t;

/**
 * Check if output stream supports color
 *
 * @param out Output stream (typically stdout/stderr)
 * @return true if color is supported, false otherwise
 */
bool output_supports_color(FILE* out);

/**
 * Print with color
 *
 * Prints text with color if the output stream supports it.
 * Falls back to plain text if colors are not supported.
 *
 * @param text Text to print
 * @param color Color code
 * @param out Output stream
 */
void output_print_colored(
    const char* text,
    color_code_t color,
    FILE* out
);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_CLI_OUTPUT_H */