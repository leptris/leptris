/**
 * @file error.h
 * @brief Error handling for Taurus CLI
 *
 * This file defines a consistent error handling system for the CLI that
 * provides clear, actionable error messages to users.
 *
 * Design Principles:
 * - Consistent: All errors use same format
 * - Actionable: Error messages suggest fixes
 * - Contextual: Include file/line/column when available
 * - Severity-based: Warnings vs errors vs fatal
 */

#ifndef TAURUS_CLI_ERROR_H
#define TAURUS_CLI_ERROR_H

/* GCC/Clang get format-string checks; MSVC silently ignores them. */
#if defined(__GNUC__) || defined(__clang__)
#  define TAURUS_PRINTF(fmt_idx, args_idx) \
       __attribute__((format(printf, fmt_idx, args_idx)))
#  define TAURUS_NORETURN __attribute__((noreturn))
#else
#  define TAURUS_PRINTF(fmt_idx, args_idx)
#  define TAURUS_NORETURN
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Error Severity Levels                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Error severity levels
 *
 * Used to classify errors and determine appropriate response:
 * - WARNING: Proceed with caution
 * - ERROR: Operation failed, but process continues
 * - FATAL: Operation failed, process must exit
 */
typedef enum {
    ERROR_LEVEL_WARNING,    /**< Warning (proceed with caution) */
    ERROR_LEVEL_ERROR,      /**< Error (operation failed) */
    ERROR_LEVEL_FATAL       /**< Fatal error (must exit) */
} error_level_t;

/**
 * Convert error level to string
 *
 * @param level Error level
 * @return String representation ("warning", "error", "fatal")
 */
const char* error_level_to_string(error_level_t level);

/* ------------------------------------------------------------------------- */
/* Error Context Structure                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Error context
 *
 * Provides complete context about an error including location information
 * and suggested fixes.
 */
typedef struct cli_error {
    error_level_t level;        /**< Error severity */
    const char* message;        /**< Error message */
    const char* file;           /**< File where error occurred (or NULL) */
    int line;                   /**< Line number (or -1 if not applicable) */
    int column;                 /**< Column number (or -1 if not applicable) */
    const char* suggestion;     /**< Suggested fix (or NULL) */
    int exit_code;              /**< Suggested exit code */
} cli_error_t;

/**
 * Create error context
 *
 * @param level Error severity
 * @param message Error message
 * @return Error context or NULL on allocation failure
 */
cli_error_t* cli_error_new(error_level_t level, const char* message);

/**
 * Create error with location
 *
 * @param level Error severity
 * @param message Error message
 * @param file File name
 * @param line Line number
 * @param column Column number
 * @return Error context or NULL on allocation failure
 */
cli_error_t* cli_error_new_with_location(
    error_level_t level,
    const char* message,
    const char* file,
    int line,
    int column
);

/**
 * Free error context
 *
 * @param error Error to free
 */
void cli_error_free(cli_error_t* error);

/**
 * Set suggested fix
 *
 * @param error Error context
 * @param suggestion Suggested fix message
 */
void cli_error_set_suggestion(cli_error_t* error, const char* suggestion);

/**
 * Print error
 *
 * Prints error in standard format:
 *
 * ```
 * error: file.xml:10:5: unexpected end of element
 * suggestion: check element nesting
 * ```
 *
 * @param error Error to print
 * @param out Output stream (typically stderr)
 */
void cli_error_print(const cli_error_t* error, FILE* out);

/* ------------------------------------------------------------------------- */
/* Convenience Functions                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Print fatal error and exit
 *
 * Prints error message and exits with EXIT_FAILURE.
 * This is a convenience function for unrecoverable errors.
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void cli_fatal(const char* fmt, ...) TAURUS_NORETURN TAURUS_PRINTF(1, 2);

/**
 * Print error message
 *
 * Prints error to stderr but continues execution.
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void cli_error(const char* fmt, ...) TAURUS_PRINTF(1, 2);

/**
 * Print warning message
 *
 * Prints warning to stderr and continues execution.
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void cli_warning(const char* fmt, ...) TAURUS_PRINTF(1, 2);

/**
 * Print info message
 *
 * Prints informational message to stdout (if not in quiet mode).
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void cli_info(const char* fmt, ...) TAURUS_PRINTF(1, 2);

/**
 * Print debug message
 *
 * Prints debug message to stderr (if in verbose mode).
 *
 * @param fmt Printf-style format string
 * @param ... Format arguments
 */
void cli_debug(const char* fmt, ...) TAURUS_PRINTF(1, 2);

/* ------------------------------------------------------------------------- */
/* Error Categories                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Print parse error
 *
 * Specialized error printer for XML parsing errors.
 *
 * @param file File name
 * @param line Line number
 * @param column Column number
 * @param message Error message
 */
void cli_error_parse(
    const char* file,
    int line,
    int column,
    const char* message
);

/**
 * Print XPath error
 *
 * Specialized error printer for XPath evaluation errors.
 *
 * @param expression XPath expression that failed
 * @param message Error message
 */
void cli_error_xpath(
    const char* expression,
    const char* message
);

/**
 * Print I/O error
 *
 * Specialized error printer for file I/O errors.
 *
 * @param file File name
 * @param operation Operation that failed ("open", "read", "write")
 * @param reason System error reason (from strerror(errno))
 */
void cli_error_io(
    const char* file,
    const char* operation,
    const char* reason
);

/**
 * Print usage error
 *
 * Specialized error printer for command-line usage errors.
 *
 * @param command Command name
 * @param message Error message
 */
void cli_error_usage(
    const char* command,
    const char* message
);

/* ------------------------------------------------------------------------- */
/* Global Error State                                                        */
/* ------------------------------------------------------------------------- */

/**
 * Error handler configuration
 *
 * Global configuration for error handling behavior.
 */
typedef struct error_config {
    bool color_enabled;         /**< Use color in error messages */
    bool show_suggestions;      /**< Show suggested fixes */
    bool verbose;               /**< Show verbose errors */
    bool quiet;                 /**< Suppress warnings and info */
    FILE* error_stream;         /**< Stream for errors (default: stderr) */
    FILE* output_stream;        /**< Stream for output (default: stdout) */
} error_config_t;

/**
 * Get global error configuration
 *
 * @return Pointer to global configuration
 */
error_config_t* cli_error_get_config(void);

/**
 * Set color output
 *
 * @param enabled Whether to use color
 */
void cli_error_set_color(bool enabled);

/**
 * Set verbose mode
 *
 * @param verbose Whether to show verbose output
 */
void cli_error_set_verbose(bool verbose);

/**
 * Set quiet mode
 *
 * @param quiet Whether to suppress warnings/info
 */
void cli_error_set_quiet(bool quiet);

/**
 * Set error stream
 *
 * @param stream Output stream for errors
 */
void cli_error_set_stream(FILE* stream);

/* ------------------------------------------------------------------------- */
/* Error Statistics                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Error statistics
 *
 * Tracks number of errors/warnings during execution.
 */
typedef struct error_stats {
    int fatal_count;            /**< Number of fatal errors */
    int error_count;            /**< Number of errors */
    int warning_count;          /**< Number of warnings */
} error_stats_t;

/**
 * Get error statistics
 *
 * @return Pointer to global statistics
 */
error_stats_t* cli_error_get_stats(void);

/**
 * Reset error statistics
 *
 * Resets all counters to zero.
 */
void cli_error_reset_stats(void);

/**
 * Check if any errors occurred
 *
 * @return true if errors or fatal errors occurred, false otherwise
 */
bool cli_error_has_errors(void);

/**
 * Print error summary
 *
 * Prints summary of all errors/warnings that occurred.
 *
 * Example: "3 errors, 2 warnings"
 *
 * @param out Output stream
 */
void cli_error_print_summary(FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_CLI_ERROR_H */