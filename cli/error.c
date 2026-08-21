#define _POSIX_C_SOURCE 200809L
/**
 * @file error.c
 * @brief Error handling implementation for Leptris CLI
 */

#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------------------- */
/* Global State                                                              */
/* ------------------------------------------------------------------------- */

static error_config_t g_error_config = {
    .color_enabled = true,
    .show_suggestions = true,
    .verbose = false,
    .quiet = false,
    .error_stream = NULL,  /* Will be set to stderr on first use */
    .output_stream = NULL  /* Will be set to stdout on first use */
};

static error_stats_t g_error_stats = {
    .fatal_count = 0,
    .error_count = 0,
    .warning_count = 0
};

/* ------------------------------------------------------------------------- */
/* Helper Functions                                                          */
/* ------------------------------------------------------------------------- */

static void ensure_streams_initialized(void) {
    if (!g_error_config.error_stream) {
        g_error_config.error_stream = stderr;
    }
    if (!g_error_config.output_stream) {
        g_error_config.output_stream = stdout;
    }
}

static const char* get_color_code(error_level_t level) {
    if (!g_error_config.color_enabled) {
        return "";
    }

    switch (level) {
        case ERROR_LEVEL_WARNING:
            return "\033[33m";  /* Yellow */
        case ERROR_LEVEL_ERROR:
            return "\033[31m";  /* Red */
        case ERROR_LEVEL_FATAL:
            return "\033[1;31m"; /* Bold Red */
        default:
            return "";
    }
}

static const char* get_reset_code(void) {
    return g_error_config.color_enabled ? "\033[0m" : "";
}

/* ------------------------------------------------------------------------- */
/* Error Level Conversion                                                    */
/* ------------------------------------------------------------------------- */

const char* error_level_to_string(error_level_t level) {
    switch (level) {
        case ERROR_LEVEL_WARNING: return "warning";
        case ERROR_LEVEL_ERROR:   return "error";
        case ERROR_LEVEL_FATAL:   return "fatal";
        default:                  return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* Error Context Creation/Destruction                                       */
/* ------------------------------------------------------------------------- */

cli_error_t* cli_error_new(error_level_t level, const char* message) {
    cli_error_t* error = malloc(sizeof(cli_error_t));
    if (!error) return NULL;

    error->level = level;
    error->message = message ? strdup(message) : NULL;
    error->file = NULL;
    error->line = -1;
    error->column = -1;
    error->suggestion = NULL;
    error->exit_code = (level == ERROR_LEVEL_FATAL) ? 1 : 0;

    return error;
}

cli_error_t* cli_error_new_with_location(
    error_level_t level,
    const char* message,
    const char* file,
    int line,
    int column
) {
    cli_error_t* error = cli_error_new(level, message);
    if (!error) return NULL;

    error->file = file ? strdup(file) : NULL;
    error->line = line;
    error->column = column;

    return error;
}

void cli_error_free(cli_error_t* error) {
    if (!error) return;

    free((void*)error->message);
    free((void*)error->file);
    free((void*)error->suggestion);
    free(error);
}

void cli_error_set_suggestion(cli_error_t* error, const char* suggestion) {
    if (!error) return;

    free((void*)error->suggestion);
    error->suggestion = suggestion ? strdup(suggestion) : NULL;
}

/* ------------------------------------------------------------------------- */
/* Error Printing                                                            */
/* ------------------------------------------------------------------------- */

void cli_error_print(const cli_error_t* error, FILE* out) {
    if (!error || !out) return;

    const char* color = get_color_code(error->level);
    const char* reset = get_reset_code();
    const char* level_str = error_level_to_string(error->level);

    /* Print: level: [file:line:col:] message */
    fprintf(out, "%s%s:%s ", color, level_str, reset);

    if (error->file) {
        fprintf(out, "%s", error->file);
        if (error->line >= 0) {
            fprintf(out, ":%d", error->line);
            if (error->column >= 0) {
                fprintf(out, ":%d", error->column);
            }
        }
        fprintf(out, ": ");
    }

    fprintf(out, "%s\n", error->message);

    /* Print suggestion if available and enabled */
    if (g_error_config.show_suggestions && error->suggestion) {
        fprintf(out, "suggestion: %s\n", error->suggestion);
    }
}

/* ------------------------------------------------------------------------- */
/* Convenience Functions                                                     */
/* ------------------------------------------------------------------------- */

void cli_fatal(const char* fmt, ...) {
    ensure_streams_initialized();

    va_list args;
    va_start(args, fmt);

    fprintf(g_error_config.error_stream, "%sfatal:%s ",
            get_color_code(ERROR_LEVEL_FATAL),
            get_reset_code());
    vfprintf(g_error_config.error_stream, fmt, args);
    fprintf(g_error_config.error_stream, "\n");

    va_end(args);

    g_error_stats.fatal_count++;
    exit(1);
}

void cli_error(const char* fmt, ...) {
    ensure_streams_initialized();

    va_list args;
    va_start(args, fmt);

    fprintf(g_error_config.error_stream, "%serror:%s ",
            get_color_code(ERROR_LEVEL_ERROR),
            get_reset_code());
    vfprintf(g_error_config.error_stream, fmt, args);
    fprintf(g_error_config.error_stream, "\n");

    va_end(args);

    g_error_stats.error_count++;
}

void cli_warning(const char* fmt, ...) {
    if (g_error_config.quiet) return;

    ensure_streams_initialized();

    va_list args;
    va_start(args, fmt);

    fprintf(g_error_config.error_stream, "%swarning:%s ",
            get_color_code(ERROR_LEVEL_WARNING),
            get_reset_code());
    vfprintf(g_error_config.error_stream, fmt, args);
    fprintf(g_error_config.error_stream, "\n");

    va_end(args);

    g_error_stats.warning_count++;
}

void cli_info(const char* fmt, ...) {
    if (g_error_config.quiet) return;

    ensure_streams_initialized();

    va_list args;
    va_start(args, fmt);
    vfprintf(g_error_config.output_stream, fmt, args);
    fprintf(g_error_config.output_stream, "\n");
    va_end(args);
}

void cli_debug(const char* fmt, ...) {
    if (!g_error_config.verbose) return;

    ensure_streams_initialized();

    va_list args;
    va_start(args, fmt);
    fprintf(g_error_config.error_stream, "debug: ");
    vfprintf(g_error_config.error_stream, fmt, args);
    fprintf(g_error_config.error_stream, "\n");
    va_end(args);
}

/* ------------------------------------------------------------------------- */
/* Specialized Error Functions                                              */
/* ------------------------------------------------------------------------- */

void cli_error_parse(
    const char* file,
    int line,
    int column,
    const char* message
) {
    cli_error_t* error = cli_error_new_with_location(
        ERROR_LEVEL_ERROR,
        message,
        file,
        line,
        column
    );

    if (error) {
        cli_error_print(error, g_error_config.error_stream);
        cli_error_free(error);
    }

    g_error_stats.error_count++;
}

void cli_error_xpath(const char* expression, const char* message) {
    ensure_streams_initialized();

    fprintf(g_error_config.error_stream, "%serror:%s XPath: %s\n",
            get_color_code(ERROR_LEVEL_ERROR),
            get_reset_code(),
            message);
    fprintf(g_error_config.error_stream, "expression: %s\n", expression);

    g_error_stats.error_count++;
}

void cli_error_io(
    const char* file,
    const char* operation,
    const char* reason
) {
    ensure_streams_initialized();

    fprintf(g_error_config.error_stream, "%serror:%s %s: %s: %s\n",
            get_color_code(ERROR_LEVEL_ERROR),
            get_reset_code(),
            operation,
            file,
            reason);

    g_error_stats.error_count++;
}

void cli_error_usage(const char* command, const char* message) {
    ensure_streams_initialized();

    fprintf(g_error_config.error_stream, "%serror:%s %s\n",
            get_color_code(ERROR_LEVEL_ERROR),
            get_reset_code(),
            message);
    fprintf(g_error_config.error_stream,
            "Use 'leptris %s --help' for usage information\n",
            command);

    g_error_stats.error_count++;
}

/* ------------------------------------------------------------------------- */
/* Global Configuration                                                      */
/* ------------------------------------------------------------------------- */

error_config_t* cli_error_get_config(void) {
    ensure_streams_initialized();
    return &g_error_config;
}

void cli_error_set_color(bool enabled) {
    g_error_config.color_enabled = enabled;
}

void cli_error_set_verbose(bool verbose) {
    g_error_config.verbose = verbose;
}

void cli_error_set_quiet(bool quiet) {
    g_error_config.quiet = quiet;
}

void cli_error_set_stream(FILE* stream) {
    g_error_config.error_stream = stream;
}

/* ------------------------------------------------------------------------- */
/* Error Statistics                                                          */
/* ------------------------------------------------------------------------- */

error_stats_t* cli_error_get_stats(void) {
    return &g_error_stats;
}

void cli_error_reset_stats(void) {
    g_error_stats.fatal_count = 0;
    g_error_stats.error_count = 0;
    g_error_stats.warning_count = 0;
}

bool cli_error_has_errors(void) {
    return (g_error_stats.error_count > 0 ||
            g_error_stats.fatal_count > 0);
}

void cli_error_print_summary(FILE* out) {
    if (!out) return;

    if (g_error_stats.fatal_count > 0) {
        fprintf(out, "%d fatal error%s",
                g_error_stats.fatal_count,
                g_error_stats.fatal_count == 1 ? "" : "s");

        if (g_error_stats.error_count > 0 ||
            g_error_stats.warning_count > 0) {
            fprintf(out, ", ");
        }
    }

    if (g_error_stats.error_count > 0) {
        fprintf(out, "%d error%s",
                g_error_stats.error_count,
                g_error_stats.error_count == 1 ? "" : "s");

        if (g_error_stats.warning_count > 0) {
            fprintf(out, ", ");
        }
    }

    if (g_error_stats.warning_count > 0) {
        fprintf(out, "%d warning%s",
                g_error_stats.warning_count,
                g_error_stats.warning_count == 1 ? "" : "s");
    }

    if (g_error_stats.fatal_count > 0 ||
        g_error_stats.error_count > 0 ||
        g_error_stats.warning_count > 0) {
        fprintf(out, "\n");
    }
}