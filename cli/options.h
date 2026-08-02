/**
 * @file options.h
 * @brief Option handling for Taurus CLI
 *
 * This file defines the MECE (Mutually Exclusive, Collectively Exhaustive)
 * option handling system for the Taurus CLI.
 *
 * Option Resolution Hierarchy (most specific wins):
 * 1. CLI arguments (--option value)
 * 2. Environment variables (TAURUS_OPTION)
 * 3. API defaults (hardcoded)
 *
 * Design Principles:
 * - MECE: All option sources covered, no overlap
 * - Explicit precedence: Clear resolution order
 * - Traceable: Can determine where option came from
 * - Type-safe: Separate int/string/bool getters
 */

#ifndef TAURUS_CLI_OPTIONS_H
#define TAURUS_CLI_OPTIONS_H

#include "cli.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Option Source Tracking                                                    */
/* ------------------------------------------------------------------------- */

/**
 * Option source enumeration
 *
 * Used for debugging and error messages to show where an option value
 * came from. This helps users understand option precedence.
 */
typedef enum {
    OPTION_SOURCE_DEFAULT,  /**< Hardcoded default value */
    OPTION_SOURCE_ENV,      /**< Environment variable */
    OPTION_SOURCE_CLI       /**< Command-line argument */
} option_source_t;

/**
 * Convert option source to string
 *
 * @param source Source to convert
 * @return String representation ("default", "env", "cli")
 */
const char* option_source_to_string(option_source_t source);

/* ------------------------------------------------------------------------- */
/* Global Options (Common to All Commands)                                  */
/* ------------------------------------------------------------------------- */

/**
 * Global options structure
 *
 * These options are available to all commands and are parsed before
 * the command is dispatched.
 *
 * Example:
 *   taurus --verbose --format json parse file.xml
 *           ^^^^^^^^^ ^^^^^^^^^^^^ global options
 */
typedef struct cli_global_options {
    /* Verbosity */
    int verbose;                /**< -v, --verbose (can be repeated) */
    int quiet;                  /**< -q, --quiet (suppress output) */
    option_source_t verbose_source;
    option_source_t quiet_source;

    /* Output format */
    const char* format;         /**< --format xml|json|text */
    option_source_t format_source;

    /* Color output */
    bool color;                 /**< --color, --no-color */
    option_source_t color_source;

    /* Help/version flags */
    bool help;                  /**< -h, --help */
    bool version;               /**< --version */
} cli_global_options_t;

/**
 * Create global options with defaults
 *
 * @return Pointer to new options, or NULL on allocation failure
 */
cli_global_options_t* cli_global_options_new(void);

/**
 * Free global options
 *
 * @param options Options to free
 */
void cli_global_options_free(cli_global_options_t* options);

/**
 * Parse global options from argv
 *
 * Parses global options and removes them from argv. After this call,
 * argv will contain only the command name and command-specific arguments.
 *
 * @param options Options structure to populate
 * @param argc Pointer to argument count (will be modified)
 * @param argv Pointer to argument array (will be modified)
 * @return CLI_SUCCESS or error code
 */
cli_result_t cli_global_options_parse(
    cli_global_options_t* options,
    int* argc,
    char*** argv
);

/* ------------------------------------------------------------------------- */
/* Command-Specific Options                                                 */
/* ------------------------------------------------------------------------- */

/**
 * Parse command options
 *
 * Options for 'taurus parse' command
 */
typedef struct cli_parse_options {
    const char* input_file;     /**< Input file (required) */
    bool validate;              /**< --validate: DTD validation */
    bool recover;               /**< --recover: Error recovery */
    bool noout;                 /**< --noout: No output */
    option_source_t validate_source;
    option_source_t recover_source;
} cli_parse_options_t;

/**
 * XPath command options
 *
 * Options for 'taurus xpath' command
 */
typedef struct cli_xpath_options {
    const char* input_file;     /**< Input file (required) */
    const char* expression;     /**< XPath expression (required) */
    bool count;                 /**< --count: Count results */
    bool boolean;               /**< --boolean: Boolean result only */
    const char* namespace_file; /**< --nsfile FILE: Namespace bindings */
    option_source_t count_source;
    option_source_t boolean_source;
} cli_xpath_options_t;

/**
 * Format command options
 *
 * Options for 'taurus format' command
 */
typedef struct cli_format_options {
    const char* input_file;     /**< Input file (required) */
    const char* output_file;    /**< -o, --output: Output file (stdout if NULL) */
    int indent;                 /**< --indent N: Indentation size */
    bool compact;               /**< --compact: Remove whitespace */
    bool encode;                /**< --encode ENC: Output encoding */
    const char* encoding;       /**< Encoding name (default: UTF-8) */
    option_source_t indent_source;
    option_source_t compact_source;
    option_source_t encoding_source;
} cli_format_options_t;

/**
 * Version command options
 *
 * Options for 'taurus version' command (minimal)
 */
typedef struct cli_version_options {
    bool short_form;            /**< --short: Just version number */
} cli_version_options_t;

/* Create/free functions for each command's options */
cli_parse_options_t* cli_parse_options_new(void);
void cli_parse_options_free(cli_parse_options_t* options);
cli_result_t cli_parse_options_parse(cli_parse_options_t* options, int argc, char** argv);

cli_xpath_options_t* cli_xpath_options_new(void);
void cli_xpath_options_free(cli_xpath_options_t* options);
cli_result_t cli_xpath_options_parse(cli_xpath_options_t* options, int argc, char** argv);

cli_format_options_t* cli_format_options_new(void);
void cli_format_options_free(cli_format_options_t* options);
cli_result_t cli_format_options_parse(cli_format_options_t* options, int argc, char** argv);

cli_version_options_t* cli_version_options_new(void);
void cli_version_options_free(cli_version_options_t* options);
cli_result_t cli_version_options_parse(cli_version_options_t* options, int argc, char** argv);

/* ------------------------------------------------------------------------- */
/* Option Parser (Generic)                                                  */
/* ------------------------------------------------------------------------- */

/**
 * Generic option parser
 *
 * State machine for parsing command-line arguments. Handles both
 * short options (-v) and long options (--verbose).
 */
typedef struct option_parser {
    int argc;                   /**< Argument count */
    char** argv;                /**< Argument array */
    int pos;                    /**< Current position in argv */
} option_parser_t;

/**
 * Create option parser
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return Parser instance
 */
option_parser_t option_parser_new(int argc, char** argv);

/**
 * Check if more arguments available
 *
 * @param parser Parser instance
 * @return true if more arguments, false otherwise
 */
bool option_parser_has_more(option_parser_t* parser);

/**
 * Get current argument
 *
 * @param parser Parser instance
 * @return Current argument or NULL if at end
 */
const char* option_parser_current(option_parser_t* parser);

/**
 * Advance to next argument
 *
 * @param parser Parser instance
 */
void option_parser_advance(option_parser_t* parser);

/**
 * Check if current argument matches option
 *
 * @param parser Parser instance
 * @param short_opt Short option (e.g., "-v") or NULL
 * @param long_opt Long option (e.g., "--verbose") or NULL
 * @return true if matches, false otherwise
 */
bool option_parser_match(
    option_parser_t* parser,
    const char* short_opt,
    const char* long_opt
);

/**
 * Get option value (for options with values)
 *
 * Advances parser past the value.
 *
 * @param parser Parser instance
 * @return Option value or NULL if no value
 */
const char* option_parser_get_value(option_parser_t* parser);

/**
 * Get remaining positional arguments
 *
 * Returns all arguments that don't start with '-'
 *
 * @param parser Parser instance
 * @param count Output: number of arguments
 * @return Array of argument strings
 */
char** option_parser_get_positional(option_parser_t* parser, int* count);

/* ------------------------------------------------------------------------- */
/* Environment Variable Helpers (MECE Layer 2)                              */
/* ------------------------------------------------------------------------- */

/**
 * Get string option from environment
 *
 * Checks TAURUS_{NAME} environment variable.
 *
 * Example: get_env_string("FORMAT") checks TAURUS_FORMAT
 *
 * @param name Option name (uppercase)
 * @param default_value Default if not set
 * @return Option value (env or default)
 */
const char* get_env_string(const char* name, const char* default_value);

/**
 * Get integer option from environment
 *
 * @param name Option name (uppercase)
 * @param default_value Default if not set
 * @return Option value (env or default)
 */
int get_env_int(const char* name, int default_value);

/**
 * Get boolean option from environment
 *
 * Accepts: 1, true, yes, on (case-insensitive)
 *
 * @param name Option name (uppercase)
 * @param default_value Default if not set
 * @return Option value (env or default)
 */
bool get_env_bool(const char* name, bool default_value);

/* ------------------------------------------------------------------------- */
/* Option Resolution (MECE Enforcement)                                     */
/* ------------------------------------------------------------------------- */

/**
 * Resolve integer option
 *
 * Implements MECE hierarchy: CLI > ENV > Default
 *
 * @param cli_value Value from CLI (or -1 if not specified)
 * @param cli_specified Whether CLI value was explicitly set
 * @param env_name Environment variable name
 * @param default_value Default value
 * @param out_source Output: where value came from
 * @return Resolved value
 */
int resolve_int_option(
    int cli_value,
    bool cli_specified,
    const char* env_name,
    int default_value,
    option_source_t* out_source
);

/**
 * Resolve string option
 *
 * @param cli_value Value from CLI (or NULL if not specified)
 * @param env_name Environment variable name
 * @param default_value Default value
 * @param out_source Output: where value came from
 * @return Resolved value
 */
const char* resolve_string_option(
    const char* cli_value,
    const char* env_name,
    const char* default_value,
    option_source_t* out_source
);

/**
 * Resolve boolean option
 *
 * @param cli_value Value from CLI
 * @param cli_specified Whether CLI value was explicitly set
 * @param env_name Environment variable name
 * @param default_value Default value
 * @param out_source Output: where value came from
 * @return Resolved value
 */
bool resolve_bool_option(
    bool cli_value,
    bool cli_specified,
    const char* env_name,
    bool default_value,
    option_source_t* out_source
);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_CLI_OPTIONS_H */