/**
 * @file parse.c
 * @brief Parse command implementation
 */

#include "../cli.h"
#include "../options.h"
#include "../error.h"
#include "../output.h"
#include "../src/include/leptris.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------------- */
/* Parse Command Options                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char* input_file;
    bool validate;
    bool recover;
    bool noout;
    bool verbose;
    output_format_t format;
} parse_options_t;

static parse_options_t* parse_options_new(void) {
    parse_options_t* opts = malloc(sizeof(parse_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->validate = false;
    opts->recover = false;
    opts->noout = false;
    opts->verbose = false;
    opts->format = OUTPUT_FORMAT_XML;

    return opts;
}

static void parse_options_free(parse_options_t* opts) {
    if (opts) {
        free(opts);
    }
}

static cli_result_t parse_options_parse(
    parse_options_t* opts,
    int argc,
    char** argv
) {
    if (!opts) return CLI_ERROR_ARGS;

    /* Skip command name */
    int i = 1;

    while (i < argc) {
        if (strcmp(argv[i], "--validate") == 0) {
            opts->validate = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--recover") == 0) {
            opts->recover = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--noout") == 0) {
            opts->noout = true;
            i++;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                cli_error("--format requires an argument");
                return CLI_ERROR_ARGS;
            }
            const char* format_str = argv[i + 1];
            output_format_t fmt = output_format_from_string(format_str);
            /* Validate format string */
            if (strcmp(format_str, "xml") != 0 &&
                strcmp(format_str, "json") != 0 &&
                strcmp(format_str, "text") != 0) {
                cli_error("invalid format: %s (must be xml, json, or text)", format_str);
                return CLI_ERROR_ARGS;
            }
            opts->format = fmt;
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
            return CLI_ERROR_ARGS; /* Trigger help */
        }
        else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            cli_error("unknown option: %s", argv[i]);
            return CLI_ERROR_ARGS;
        }
        else {
            /* Positional argument - input file (including "-" for stdin) */
            if (opts->input_file) {
                cli_error("multiple input files not supported");
                return CLI_ERROR_ARGS;
            }
            opts->input_file = argv[i];
            i++;
        }
    }

    /* Check required arguments */
    if (!opts->input_file) {
        cli_error("missing required argument: FILE");
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

        /* Allocate buffer */
        char* buffer = malloc(size + 1);
        if (!buffer) {
            if (!is_stdin) fclose(f);
            return NULL;
        }

        /* Read file */
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
/* Parse Command Implementation                                              */
/* ------------------------------------------------------------------------- */

static cli_result_t parse_execute(int argc, char** argv) {
    cli_result_t result = CLI_SUCCESS;
    parse_options_t* opts = NULL;
    char* xml_content = NULL;
    struct leptris_document* doc = NULL;
    output_formatter_t* fmt = NULL;

    /* Parse options */
    opts = parse_options_new();
    if (!opts) {
        cli_fatal("cannot allocate memory");
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    if (parse_options_parse(opts, argc, argv) != CLI_SUCCESS) {
        result = CLI_ERROR_ARGS;
        goto cleanup;
    }

    /* Read input file */
    size_t xml_len;
    xml_content = read_file(opts->input_file, &xml_len);
    if (!xml_content) {
        cli_error("cannot read file: %s: %s",
                  opts->input_file, strerror(errno));
        result = CLI_ERROR_IO;
        goto cleanup;
    }

    /* Parse XML - use encoding-aware parser for UTF-16 support */
    LeptrisStatus status;
    doc = leptris_parse_string_with_encoding(xml_content, xml_len, &status);
    if (!doc) {
        cli_error("failed to parse XML: %s", opts->input_file);
        result = CLI_ERROR_PARSE;
        goto cleanup;
    }

    /* Verbose output - print document info to stderr */
    if (opts->verbose) {
        LeptrisElement root = leptris_document_root(doc);
        const char* root_name = root ? leptris_element_name(root) : "(unknown)";
        fprintf(stderr, "[leptris] Parsed: %s\n", opts->input_file);
        fprintf(stderr, "[leptris] Root element: %s\n", root_name);
        fprintf(stderr, "[leptris] Size: %zu bytes\n", xml_len);
        fflush(stderr);
    }

    /* Output result (unless --noout) */
    if (!opts->noout) {
        if (opts->format == OUTPUT_FORMAT_XML) {
            /* For XML output, just pass through original content */
            /* TODO: Implement proper serialization when API is available */
            printf("%s", xml_content);
        } else {
            /* Use formatter for JSON/text output */
            fmt = output_formatter_create(opts->format);
            if (!fmt) {
                cli_error("cannot create formatter");
                result = CLI_ERROR_MEMORY;
                goto cleanup;
            }

            fmt->print_document(doc, stdout, fmt->context);
        }
    }

    /* Success message if validate mode is on */
    if (opts->validate && !opts->noout) {
        cli_info("parsed successfully: %s", opts->input_file);
    }

cleanup:
    if (fmt) output_formatter_free(fmt);
    if (doc) leptris_document_free(doc);
    if (xml_content) free(xml_content);
    if (opts) parse_options_free(opts);

    return result;
}

static void parse_print_help(void) {
    printf("Usage: leptris parse [OPTIONS] FILE\n");
    printf("\n");
    printf("Parse and validate XML document.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  FILE                Input XML file\n");
    printf("\n");
    printf("Options:\n");
    printf("  --validate          Enable validation (not yet implemented)\n");
    printf("  --recover           Enable error recovery (not yet implemented)\n");
    printf("  --noout             Suppress output (only check for errors)\n");
    printf("  -f, --format FORMAT Output format: xml|json|text (default: xml)\n");
    printf("  -v, --verbose       Show verbose output\n");
    printf("  -q, --quiet         Suppress warnings\n");
    printf("  -h, --help          Show this help\n");
    printf("\n");
    printf("Exit codes:\n");
    printf("  0    Success\n");
    printf("  1    Parse error\n");
    printf("  3    I/O error\n");
    printf("  4    Invalid arguments\n");
}

/* ------------------------------------------------------------------------- */
/* Command Registration                                                      */
/* ------------------------------------------------------------------------- */

static cli_command_t parse_command = {
    .name = "parse",
    .description = "Parse and validate XML",
    .execute = parse_execute,
    .print_help = parse_print_help
};

cli_command_t* cli_command_parse(void) {
    return &parse_command;
}