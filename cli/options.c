/**
 * @file options.c
 * @brief Option parsing implementation for Leptris CLI
 */

#include "options.h"
#include "cli.h"
#include "error.h"
#include "../src/leptris/common/port.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------------- */
/* Option Source Conversion                                                  */
/* ------------------------------------------------------------------------- */

const char* option_source_to_string(option_source_t source) {
    switch (source) {
        case OPTION_SOURCE_DEFAULT: return "default";
        case OPTION_SOURCE_ENV:     return "env";
        case OPTION_SOURCE_CLI:     return "cli";
        default:                    return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* Global Options                                                            */
/* ------------------------------------------------------------------------- */

cli_global_options_t* cli_global_options_new(void) {
    cli_global_options_t* opts = malloc(sizeof(cli_global_options_t));
    if (!opts) return NULL;

    /* Initialize with defaults */
    opts->verbose = 0;
    opts->quiet = 0;
    opts->format = "xml";
    opts->color = true;
    opts->help = false;
    opts->version = false;

    opts->verbose_source = OPTION_SOURCE_DEFAULT;
    opts->quiet_source = OPTION_SOURCE_DEFAULT;
    opts->format_source = OPTION_SOURCE_DEFAULT;
    opts->color_source = OPTION_SOURCE_DEFAULT;

    return opts;
}

void cli_global_options_free(cli_global_options_t* options) {
    if (!options) return;
    free(options);
}

cli_result_t cli_global_options_parse(
    cli_global_options_t* options,
    int* argc,
    char*** argv
) {
    if (!options || !argc || !argv) {
        return CLI_ERROR_ARGS;
    }

    option_parser_t parser = option_parser_new(*argc, *argv);

    /* Skip program name */
    option_parser_advance(&parser);

    /* Build new argv without global options */
    char** new_argv = malloc(sizeof(char*) * (*argc));
    if (!new_argv) return CLI_ERROR_MEMORY;

    int new_argc = 0;
    new_argv[new_argc++] = (*argv)[0]; /* Keep program name */

    while (option_parser_has_more(&parser)) {
        const char* arg = option_parser_current(&parser);

        /* Note: --verbose and --quiet are passed through to commands, not consumed here */
        if (option_parser_match(&parser, "-v", "--verbose")) {
            options->verbose++;
            options->verbose_source = OPTION_SOURCE_CLI;
            option_parser_advance(&parser);
            /* Keep in argv so commands can see it */
            new_argv[new_argc++] = (char*)arg;
        }
        else if (option_parser_match(&parser, "-q", "--quiet")) {
            options->quiet = 1;
            options->quiet_source = OPTION_SOURCE_CLI;
            option_parser_advance(&parser);
            /* Keep in argv so commands can see it */
            new_argv[new_argc++] = (char*)arg;
        }
        else if (option_parser_match(&parser, NULL, "--color")) {
            options->color = true;
            options->color_source = OPTION_SOURCE_CLI;
            option_parser_advance(&parser);
        }
        else if (option_parser_match(&parser, NULL, "--no-color")) {
            options->color = false;
            options->color_source = OPTION_SOURCE_CLI;
            option_parser_advance(&parser);
        }
        else if (option_parser_match(&parser, "-h", "--help")) {
            options->help = true;
            option_parser_advance(&parser);
        }
        else if (option_parser_match(&parser, NULL, "--version")) {
            options->version = true;
            option_parser_advance(&parser);
        }
        else {
            /* Not a global option - keep in argv */
            new_argv[new_argc++] = (char*)arg;
            option_parser_advance(&parser);
        }
    }

    /* Update argc/argv */
    *argc = new_argc;
    memcpy(*argv, new_argv, sizeof(char*) * new_argc);
    free(new_argv);

    return CLI_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Generic Option Parser                                                     */
/* ------------------------------------------------------------------------- */

option_parser_t option_parser_new(int argc, char** argv) {
    option_parser_t parser;
    parser.argc = argc;
    parser.argv = argv;
    parser.pos = 0;
    return parser;
}

bool option_parser_has_more(option_parser_t* parser) {
    return parser && parser->pos < parser->argc;
}

const char* option_parser_current(option_parser_t* parser) {
    if (!parser || parser->pos >= parser->argc) {
        return NULL;
    }
    return parser->argv[parser->pos];
}

void option_parser_advance(option_parser_t* parser) {
    if (parser && parser->pos < parser->argc) {
        parser->pos++;
    }
}

bool option_parser_match(
    option_parser_t* parser,
    const char* short_opt,
    const char* long_opt
) {
    const char* arg = option_parser_current(parser);
    if (!arg) return false;

    if (short_opt && strcmp(arg, short_opt) == 0) {
        return true;
    }

    if (long_opt && strcmp(arg, long_opt) == 0) {
        return true;
    }

    return false;
}

const char* option_parser_get_value(option_parser_t* parser) {
    if (!parser || parser->pos >= parser->argc) {
        return NULL;
    }
    return parser->argv[parser->pos];
}

char** option_parser_get_positional(option_parser_t* parser, int* count) {
    if (!parser || !count) return NULL;

    *count = 0;
    for (int i = parser->pos; i < parser->argc; i++) {
        if (parser->argv[i][0] != '-') {
            (*count)++;
        }
    }

    if (*count == 0) return NULL;

    char** result = malloc(sizeof(char*) * (*count));
    if (!result) return NULL;

    int idx = 0;
    for (int i = parser->pos; i < parser->argc; i++) {
        if (parser->argv[i][0] != '-') {
            result[idx++] = parser->argv[i];
        }
    }

    return result;
}

/* ------------------------------------------------------------------------- */
/* Environment Variable Helpers                                              */
/* ------------------------------------------------------------------------- */

const char* get_env_string(const char* name, const char* default_value) {
    if (!name) return default_value;

    /* Build LEPTRIS_NAME */
    char env_name[256];
    snprintf(env_name, sizeof(env_name), "LEPTRIS_%s", name);

    const char* value = getenv(env_name);
    return value ? value : default_value;
}

int get_env_int(const char* name, int default_value) {
    const char* str = get_env_string(name, NULL);
    if (!str) return default_value;

    return atoi(str);
}

bool get_env_bool(const char* name, bool default_value) {
    const char* str = get_env_string(name, NULL);
    if (!str) return default_value;

    /* Accept: 1, true, yes, on (case-insensitive) */
    if (strcmp(str, "1") == 0 ||
        strcasecmp(str, "true") == 0 ||
        strcasecmp(str, "yes") == 0 ||
        strcasecmp(str, "on") == 0) {
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------------- */
/* Option Resolution (MECE Enforcement)                                      */
/* ------------------------------------------------------------------------- */

int resolve_int_option(
    int cli_value,
    bool cli_specified,
    const char* env_name,
    int default_value,
    option_source_t* out_source
) {
    /* MECE hierarchy: CLI > ENV > Default */

    if (cli_specified) {
        if (out_source) *out_source = OPTION_SOURCE_CLI;
        return cli_value;
    }

    const char* env_str = get_env_string(env_name, NULL);
    if (env_str) {
        if (out_source) *out_source = OPTION_SOURCE_ENV;
        return atoi(env_str);
    }

    if (out_source) *out_source = OPTION_SOURCE_DEFAULT;
    return default_value;
}

const char* resolve_string_option(
    const char* cli_value,
    const char* env_name,
    const char* default_value,
    option_source_t* out_source
) {
    /* MECE hierarchy: CLI > ENV > Default */

    if (cli_value) {
        if (out_source) *out_source = OPTION_SOURCE_CLI;
        return cli_value;
    }

    const char* env_value = get_env_string(env_name, NULL);
    if (env_value) {
        if (out_source) *out_source = OPTION_SOURCE_ENV;
        return env_value;
    }

    if (out_source) *out_source = OPTION_SOURCE_DEFAULT;
    return default_value;
}

bool resolve_bool_option(
    bool cli_value,
    bool cli_specified,
    const char* env_name,
    bool default_value,
    option_source_t* out_source
) {
    /* MECE hierarchy: CLI > ENV > Default */

    if (cli_specified) {
        if (out_source) *out_source = OPTION_SOURCE_CLI;
        return cli_value;
    }

    const char* env_str = get_env_string(env_name, NULL);
    if (env_str) {
        if (out_source) *out_source = OPTION_SOURCE_ENV;
        return get_env_bool(env_name, default_value);
    }

    if (out_source) *out_source = OPTION_SOURCE_DEFAULT;
    return default_value;
}

/* ------------------------------------------------------------------------- */
/* Command-Specific Options (Stubs for now)                                 */
/* ------------------------------------------------------------------------- */

cli_parse_options_t* cli_parse_options_new(void) {
    cli_parse_options_t* opts = malloc(sizeof(cli_parse_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->validate = false;
    opts->recover = false;
    opts->noout = false;
    opts->validate_source = OPTION_SOURCE_DEFAULT;
    opts->recover_source = OPTION_SOURCE_DEFAULT;

    return opts;
}

void cli_parse_options_free(cli_parse_options_t* options) {
    free(options);
}

cli_result_t cli_parse_options_parse(
    cli_parse_options_t* options,
    int argc,
    char** argv
) {
    /* Stub - will implement in Session 94 */
    (void)options;
    (void)argc;
    (void)argv;
    return CLI_SUCCESS;
}

cli_xpath_options_t* cli_xpath_options_new(void) {
    cli_xpath_options_t* opts = malloc(sizeof(cli_xpath_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->expression = NULL;
    opts->count = false;
    opts->boolean = false;
    opts->namespace_file = NULL;
    opts->count_source = OPTION_SOURCE_DEFAULT;
    opts->boolean_source = OPTION_SOURCE_DEFAULT;

    return opts;
}

void cli_xpath_options_free(cli_xpath_options_t* options) {
    free(options);
}

cli_result_t cli_xpath_options_parse(
    cli_xpath_options_t* options,
    int argc,
    char** argv
) {
    /* Stub - will implement in Session 94 */
    (void)options;
    (void)argc;
    (void)argv;
    return CLI_SUCCESS;
}

cli_format_options_t* cli_format_options_new(void) {
    cli_format_options_t* opts = malloc(sizeof(cli_format_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->output_file = NULL;
    opts->indent = 2;
    opts->compact = false;
    opts->encode = false;
    opts->encoding = "UTF-8";
    opts->indent_source = OPTION_SOURCE_DEFAULT;
    opts->compact_source = OPTION_SOURCE_DEFAULT;
    opts->encoding_source = OPTION_SOURCE_DEFAULT;

    return opts;
}

void cli_format_options_free(cli_format_options_t* options) {
    free(options);
}

cli_result_t cli_format_options_parse(
    cli_format_options_t* options,
    int argc,
    char** argv
) {
    /* Stub - will implement in Session 94 */
    (void)options;
    (void)argc;
    (void)argv;
    return CLI_SUCCESS;
}

cli_version_options_t* cli_version_options_new(void) {
    cli_version_options_t* opts = malloc(sizeof(cli_version_options_t));
    if (!opts) return NULL;

    opts->short_form = false;

    return opts;
}

void cli_version_options_free(cli_version_options_t* options) {
    free(options);
}

cli_result_t cli_version_options_parse(
    cli_version_options_t* options,
    int argc,
    char** argv
) {
    /* Simple parsing for version command */
    option_parser_t parser = option_parser_new(argc, argv);
    option_parser_advance(&parser); /* Skip command name */

    while (option_parser_has_more(&parser)) {
        if (option_parser_match(&parser, "-s", "--short")) {
            options->short_form = true;
            option_parser_advance(&parser);
        }
        else {
            option_parser_advance(&parser);
        }
    }

    return CLI_SUCCESS;
}