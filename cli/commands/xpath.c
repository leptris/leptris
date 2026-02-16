/**
 * @file xpath.c
 * @brief XPath command implementation
 */

#include "../cli.h"
#include "../options.h"
#include "../error.h"
#include "../output.h"
#include "../src/include/taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Need access to internal structures for result handling */
#include "../../src/taurus/taurus_internal.h"

/* Forward declarations */
static void xpath_print_help(void);

/* ------------------------------------------------------------------------- */
/* XPath Command Options                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char* input_file;
    const char* expression;
    bool count_only;
    bool boolean_only;
    bool verbose;
    const char* nsfile;
    output_format_t format;
} xpath_options_t;

static xpath_options_t* xpath_options_new(void) {
    xpath_options_t* opts = malloc(sizeof(xpath_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->expression = NULL;
    opts->count_only = false;
    opts->boolean_only = false;
    opts->verbose = false;
    opts->nsfile = NULL;
    opts->format = OUTPUT_FORMAT_XML;

    return opts;
}

static void xpath_options_free(xpath_options_t* opts) {
    if (opts) {
        free(opts);
    }
}

static cli_result_t xpath_options_parse(
    xpath_options_t* opts,
    int argc,
    char** argv
) {
    if (!opts) return CLI_ERROR_ARGS;

    /* Skip command name */
    int i = 1;
    int positional_count = 0;

    while (i < argc) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0) {
            opts->count_only = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--boolean") == 0) {
            opts->boolean_only = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--nsfile") == 0) {
            if (i + 1 >= argc) {
                cli_error("--nsfile requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->nsfile = argv[i + 1];
            i += 2;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                cli_error("--format requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->format = output_format_from_string(argv[i + 1]);
            i += 2;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            /* Quiet mode - suppress warnings (verbose takes precedence) */
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "--help") == 0) {
            return CLI_HELP; /* Request help display */
        }
        else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            cli_error("unknown option: %s", argv[i]);
            return CLI_ERROR_ARGS;
        }
        else {
            /* Positional arguments: FILE EXPRESSION (including "-" for stdin) */
            if (positional_count == 0) {
                opts->input_file = argv[i];
            } else if (positional_count == 1) {
                opts->expression = argv[i];
            } else {
                cli_error("too many arguments");
                return CLI_ERROR_ARGS;
            }
            positional_count++;
            i++;
        }
    }

    /* Check required arguments */
    if (!opts->input_file) {
        cli_error("missing required argument: FILE");
        return CLI_ERROR_ARGS;
    }
    if (!opts->expression) {
        cli_error("missing required argument: EXPRESSION");
        return CLI_ERROR_ARGS;
    }

    /* Validate option combinations */
    if (opts->count_only && opts->boolean_only) {
        cli_error("--count and --boolean are mutually exclusive");
        return CLI_ERROR_ARGS;
    }

    return CLI_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* File I/O Helpers                                                          */
/* ------------------------------------------------------------------------- */

static char* read_file(const char* filename, size_t* out_len) {
    FILE* f;
    int is_stdin = 0;

    /* Handle stdin with "-" filename */
    if (strcmp(filename, "-") == 0) {
        f = stdin;
        is_stdin = 1;
    } else {
        f = fopen(filename, "rb");
        if (!f) {
            return NULL;
        }
    }

    /* Get file size (for stdin, read until EOF) */
    long size;
    if (is_stdin) {
        /* For stdin, we need to read dynamically */
        size_t capacity = 4096;
        size_t offset = 0;
        char* buffer = malloc(capacity);
        if (!buffer) {
            return NULL;
        }

        while (!feof(f)) {
            /* Ensure we have space for more data */
            if (offset + 4096 > capacity) {
                capacity *= 2;
                char* new_buffer = realloc(buffer, capacity);
                if (!new_buffer) {
                    free(buffer);
                    return NULL;
                }
                buffer = new_buffer;
            }

            /* Read a chunk */
            size_t read = fread(buffer + offset, 1, 4096, f);
            offset += read;

            /* Check for read errors */
            if (ferror(f)) {
                free(buffer);
                return NULL;
            }
        }

        buffer[offset] = '\0';
        *out_len = offset;
        return buffer;
    } else {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size < 0) {
            if (!is_stdin) fclose(f);
            return NULL;
        }

        char* buffer = malloc(size + 1);
        if (!buffer) {
            if (!is_stdin) fclose(f);
            return NULL;
        }

        size_t read = fread(buffer, 1, size, f);
        if (!is_stdin) fclose(f);

        if (read != (size_t)size) {
            free(buffer);
            return NULL;
        }

        buffer[size] = '\0';
        *out_len = size;

        return buffer;
    }
}

/* ------------------------------------------------------------------------- */
/* XPath Command Implementation                                              */
/* ------------------------------------------------------------------------- */

static cli_result_t xpath_execute(int argc, char** argv) {
    cli_result_t result = CLI_SUCCESS;
    xpath_options_t* opts = NULL;
    char* xml_content = NULL;
    struct taurus_document* doc = NULL;
    struct taurus_xpath_result* xpath_result = NULL;
    output_formatter_t* fmt = NULL;

    /* Parse options */
    opts = xpath_options_new();
    if (!opts) {
        cli_fatal("cannot allocate memory");
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    result = xpath_options_parse(opts, argc, argv);
    if (result == CLI_HELP) {
        xpath_print_help();
        result = CLI_SUCCESS;
        goto cleanup;
    }
    if (result != CLI_SUCCESS) {
        goto cleanup;
    }

    /* Read and parse XML file */
    size_t xml_len;
    xml_content = read_file(opts->input_file, &xml_len);
    if (!xml_content) {
        cli_error("cannot read file: %s: %s",
                  opts->input_file, strerror(errno));
        result = CLI_ERROR_IO;
        goto cleanup;
    }

    TaurusStatus status;
    doc = taurus_parse_string(xml_content, xml_len, &status);
    if (!doc) {
        cli_error("failed to parse XML: %s", opts->input_file);
        result = CLI_ERROR_PARSE;
        goto cleanup;
    }

    /* Evaluate XPath expression */
    xpath_result = taurus_xpath_eval(
        doc,
        NULL,  /* context = NULL means use document root */
        opts->expression
    );

    if (!xpath_result) {
        cli_error("failed to evaluate XPath expression: %s",
                  opts->expression);
        result = CLI_ERROR_XPATH;
        goto cleanup;
    }

    /* Verbose output - print result info to stderr */
    if (opts->verbose) {
        const char* result_type = "unknown";
        switch (xpath_result->type) {
            case XPATH_RESULT_NODESET:
                result_type = "nodeset";
                break;
            case XPATH_RESULT_BOOLEAN:
                result_type = "boolean";
                break;
            case XPATH_RESULT_NUMBER:
                result_type = "number";
                break;
            case XPATH_RESULT_STRING:
                result_type = "string";
                break;
        }
        fprintf(stderr, "[taurus] Parsed: %s\n", opts->input_file);
        fprintf(stderr, "[taurus] XPath: %s\n", opts->expression);
        fprintf(stderr, "[taurus] Result type: %s\n", result_type);
        if (xpath_result->type == XPATH_RESULT_NODESET) {
            size_t count = xpath_result->value.nodeset_value->count;
            fprintf(stderr, "[taurus] Nodes selected: %zu\n", count);
        }
        fflush(stderr);
    }

    /* Create formatter */
    fmt = output_formatter_create(opts->format);
    if (!fmt) {
        cli_error("cannot create formatter");
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    /* Output based on options and result type */
    if (opts->count_only) {
        /* Count mode - output count only */
        if (xpath_result->type == XPATH_RESULT_NODESET) {
            size_t count = xpath_result->value.nodeset_value->count;
            output_print_count(count, stdout, fmt);
        } else {
            output_print_count(1, stdout, fmt);
        }
    }
    else if (opts->boolean_only) {
        /* Boolean mode - output true/false */
        int bool_val;
        if (xpath_result->type == XPATH_RESULT_BOOLEAN) {
            bool_val = xpath_result->value.boolean_value;
        } else if (xpath_result->type == XPATH_RESULT_NODESET) {
            bool_val = xpath_result->value.nodeset_value->count > 0;
        } else if (xpath_result->type == XPATH_RESULT_NUMBER) {
            bool_val = xpath_result->value.number_value != 0;
        } else {
            bool_val = xpath_result->value.string_value &&
                       xpath_result->value.string_value[0] != '\0';
        }
        fmt->print_boolean(bool_val, stdout, fmt->context);
    }
    else {
        /* Normal mode - format based on result type */
        switch (xpath_result->type) {
            case XPATH_RESULT_NODESET:
                fmt->print_nodeset(xpath_result, stdout, fmt->context);
                break;

            case XPATH_RESULT_STRING:
                fmt->print_string(
                    xpath_result->value.string_value,
                    stdout,
                    fmt->context
                );
                break;

            case XPATH_RESULT_NUMBER:
                fmt->print_number(
                    xpath_result->value.number_value,
                    stdout,
                    fmt->context
                );
                break;

            case XPATH_RESULT_BOOLEAN:
                fmt->print_boolean(
                    xpath_result->value.boolean_value,
                    stdout,
                    fmt->context
                );
                break;
        }
    }

cleanup:
    if (fmt) output_formatter_free(fmt);
    if (xpath_result) taurus_xpath_result_free(xpath_result);
    if (doc) taurus_document_free(doc);
    if (xml_content) free(xml_content);
    if (opts) xpath_options_free(opts);

    return result;
}

static void xpath_print_help(void) {
    printf("Usage: taurus xpath [OPTIONS] FILE EXPRESSION\n");
    printf("\n");
    printf("Execute XPath query against XML document.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  FILE                Input XML file\n");
    printf("  EXPRESSION          XPath expression to evaluate\n");
    printf("\n");
    printf("Options:\n");
    printf("  -c, --count         Output count of nodes only\n");
    printf("  -b, --boolean       Output boolean result only\n");
    printf("  --nsfile FILE       Namespace bindings file (not yet implemented)\n");
    printf("  -f, --format FORMAT Output format: xml|json|text (default: xml)\n");
    printf("  -v, --verbose       Show verbose output\n");
    printf("  -q, --quiet         Suppress warnings\n");
    printf("  -h, --help          Show this help\n");
    printf("\n");
    printf("Exit codes:\n");
    printf("  0    Success\n");
    printf("  2    XPath error\n");
    printf("  3    I/O error\n");
    printf("  4    Invalid arguments\n");
}

/* ------------------------------------------------------------------------- */
/* Command Registration                                                      */
/* ------------------------------------------------------------------------- */

static cli_command_t xpath_command = {
    .name = "xpath",
    .description = "Execute XPath queries",
    .execute = xpath_execute,
    .print_help = xpath_print_help
};

cli_command_t* cli_command_xpath(void) {
    return &xpath_command;
}