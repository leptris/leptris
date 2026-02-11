/**
 * @file format.c
 * @brief Format command implementation
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

/* ------------------------------------------------------------------------- */
/* Format Command Options                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char* input_file;
    const char* output_file;
    int indent;
    bool compact;
    bool verbose;
    const char* encoding;
    output_format_t format;
} format_options_t;

static format_options_t* format_options_new(void) {
    format_options_t* opts = malloc(sizeof(format_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->output_file = NULL;
    opts->indent = 2;
    opts->compact = false;
    opts->verbose = false;
    opts->encoding = "UTF-8";
    opts->format = OUTPUT_FORMAT_XML;

    return opts;
}

static void format_options_free(format_options_t* opts) {
    if (opts) {
        free(opts);
    }
}

static cli_result_t format_options_parse(
    format_options_t* opts,
    int argc,
    char** argv
) {
    if (!opts) return CLI_ERROR_ARGS;

    /* Skip command name */
    int i = 1;

    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0 ||
            strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                cli_error("--output requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->output_file = argv[i + 1];
            i += 2;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--indent") == 0) {
            if (i + 1 >= argc) {
                cli_error("--indent requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->indent = atoi(argv[i + 1]);
            if (opts->indent < 0 || opts->indent > 16) {
                cli_error("indent must be between 0 and 16");
                return CLI_ERROR_ARGS;
            }
            i += 2;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--compact") == 0) {
            opts->compact = true;
            i++;
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
        else if (strcmp(argv[i], "--encode") == 0) {
            if (i + 1 >= argc) {
                cli_error("--encode requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->encoding = argv[i + 1];
            i += 2;
            continue;  /* Skip positional argument handling */
        }
        else if (strcmp(argv[i], "--format") == 0) {
            if (i + 1 >= argc) {
                cli_error("--format requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts->format = output_format_from_string(argv[i + 1]);
            i += 2;
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

    /* Validate option combinations */
    if (opts->compact && opts->indent != 2) {
        cli_error("--compact and --indent are mutually exclusive");
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
/* Format Command Implementation                                             */
/* ------------------------------------------------------------------------- */

static cli_result_t format_execute(int argc, char** argv) {
    cli_result_t result = CLI_SUCCESS;
    format_options_t* opts = NULL;
    char* xml_content = NULL;
    struct taurus_document* doc = NULL;
    output_formatter_t* fmt = NULL;
    FILE* out = stdout;
    bool close_output = false;

    /* Parse options */
    opts = format_options_new();
    if (!opts) {
        cli_fatal("cannot allocate memory");
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    if (format_options_parse(opts, argc, argv) != CLI_SUCCESS) {
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

    /* Parse XML */
    TaurusStatus status;
    doc = taurus_parse_string(xml_content, xml_len, &status);
    if (!doc) {
        cli_error("failed to parse XML: %s", opts->input_file);
        result = CLI_ERROR_PARSE;
        goto cleanup;
    }

    /* Verbose output - print document info to stderr */
    if (opts->verbose) {
        TaurusElement root = taurus_document_root(doc);
        const char* root_name = root ? taurus_element_name(root) : "(unknown)";
        fprintf(stderr, "[taurus] Parsed: %s\n", opts->input_file);
        fprintf(stderr, "[taurus] Root element: %s\n", root_name);
        fprintf(stderr, "[taurus] Format: %s\n",
                opts->compact ? "compact" : (opts->indent == 0 ? "minimal" : "pretty"));
        if (opts->output_file) {
            fprintf(stderr, "[taurus] Output: %s\n", opts->output_file);
        }
        fflush(stderr);
    }

    /* Open output file if specified */
    if (opts->output_file) {
        out = fopen(opts->output_file, "w");
        if (!out) {
            cli_error("cannot open output file: %s: %s",
                      opts->output_file, strerror(errno));
            result = CLI_ERROR_IO;
            goto cleanup;
        }
        close_output = true;
    }

    /* Create formatter with options */
    fmt = output_formatter_create(opts->format);
    if (!fmt) {
        cli_error("cannot create formatter");
        result = CLI_ERROR_MEMORY;
        goto cleanup;
    }

    /* Set format-specific options */
    if (opts->format == OUTPUT_FORMAT_XML) {
        xml_format_options_t xml_opts;
        xml_opts.indent = opts->compact ? 0 : opts->indent;
        xml_opts.pretty_print = !opts->compact;
        xml_opts.include_declaration = true;
        xml_opts.encoding = opts->encoding;
        output_formatter_set_xml_options(fmt, &xml_opts);
    }

    /* Format and output */
    fmt->print_document(doc, out, fmt->context);

cleanup:
    if (close_output && out) fclose(out);
    if (fmt) output_formatter_free(fmt);
    if (doc) taurus_document_free(doc);
    if (xml_content) free(xml_content);
    if (opts) format_options_free(opts);

    return result;
}

static void format_print_help(void) {
    printf("Usage: taurus format [OPTIONS] FILE\n");
    printf("\n");
    printf("Pretty-print or reformat XML document.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  FILE                Input XML file\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o, --output FILE   Output file (default: stdout)\n");
    printf("  --indent N          Indentation size (default: 2)\n");
    printf("  --compact           Remove all whitespace\n");
    printf("  --encode ENCODING   Output encoding (default: UTF-8)\n");
    printf("  --format FORMAT     Output format: xml|json|text (default: xml)\n");
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

static cli_command_t format_command = {
    .name = "format",
    .description = "Pretty-print XML",
    .execute = format_execute,
    .print_help = format_print_help
};

cli_command_t* cli_command_format(void) {
    return &format_command;
}