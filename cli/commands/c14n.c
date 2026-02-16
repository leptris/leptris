/**
 * @file c14n.c
 * @brief Canonical XML (C14N) command implementation
 *
 * C14N generates a canonical form of XML for digital signatures,
 * cryptographic hashing, and semantic comparison.
 */

#include "../cli.h"
#include "../error.h"
#include "../src/include/taurus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Forward declarations */
static void c14n_print_help(void);

/* ------------------------------------------------------------------------- */
/* C14N Command Options                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char* input_file;
    const char* output_file;
    int version;        /* 0 = C14N 1.0, 1 = C14N 1.1 */
    bool verbose;
} c14n_options_t;

static c14n_options_t* c14n_options_new(void) {
    c14n_options_t* opts = malloc(sizeof(c14n_options_t));
    if (!opts) return NULL;

    opts->input_file = NULL;
    opts->output_file = NULL;
    opts->version = 0;  /* Default: C14N 1.0 */
    opts->verbose = false;

    return opts;
}

static void c14n_options_free(c14n_options_t* opts) {
    if (opts) {
        free(opts);
    }
}

static cli_result_t c14n_options_parse(
    c14n_options_t* opts,
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
            continue;
        }
        else if (strcmp(argv[i], "--version") == 0) {
            if (i + 1 >= argc) {
                cli_error("--version requires an argument (1.0 or 1.1)");
                return CLI_ERROR_ARGS;
            }
            if (strcmp(argv[i + 1], "1.0") == 0) {
                opts->version = 0;
            } else if (strcmp(argv[i + 1], "1.1") == 0) {
                opts->version = 1;
            } else {
                cli_error("--version must be '1.0' or '1.1'");
                return CLI_ERROR_ARGS;
            }
            i += 2;
            continue;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
            i++;
            continue;
        }
        else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            i++;
            continue;
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
            /* Positional argument - input file */
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

    if (strcmp(filename, "-") == 0) {
        f = stdin;
        is_stdin = 1;
    } else {
        f = fopen(filename, "rb");
        if (!f) {
            return NULL;
        }
    }

    /* Get file size */
    size_t capacity = 4096;
    size_t size = 0;
    char* content = malloc(capacity);
    if (!content) {
        if (!is_stdin) fclose(f);
        return NULL;
    }

    /* Read file */
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (size + 1 >= capacity) {
            capacity *= 2;
            char* new_content = realloc(content, capacity);
            if (!new_content) {
                free(content);
                if (!is_stdin) fclose(f);
                return NULL;
            }
            content = new_content;
        }
        content[size++] = (char)c;
    }
    content[size] = '\0';

    if (!is_stdin) fclose(f);
    if (out_len) *out_len = size;
    return content;
}

/* ------------------------------------------------------------------------- */
/* C14N Command Implementation                                               */
/* ------------------------------------------------------------------------- */

static cli_result_t c14n_execute(int argc, char** argv) {
    cli_result_t result = CLI_SUCCESS;
    c14n_options_t* opts = c14n_options_new();
    char* xml_content = NULL;
    struct taurus_document* doc = NULL;
    char* canonical = NULL;
    FILE* out = stdout;
    int close_output = 0;

    if (!opts) {
        cli_error("memory allocation failed");
        return CLI_ERROR_MEMORY;
    }

    /* Parse options */
    result = c14n_options_parse(opts, argc, argv);
    if (result == CLI_HELP) {
        c14n_print_help();
        result = CLI_SUCCESS;
        goto cleanup;
    }
    if (result != CLI_SUCCESS) {
        goto cleanup;
    }

    /* Read input file */
    size_t xml_len;
    xml_content = read_file(opts->input_file, &xml_len);
    if (!xml_content) {
        cli_error("cannot read file: %s", opts->input_file);
        result = CLI_ERROR_IO;
        goto cleanup;
    }

    if (opts->verbose) {
        fprintf(stderr, "[taurus] Parsing XML (%zu bytes)...\n", xml_len);
    }

    /* Parse XML */
    TaurusStatus status;
    doc = taurus_parse_string(xml_content, xml_len, &status);
    if (!doc) {
        cli_error("parse error (status=%d)", status);
        result = CLI_ERROR_PARSE;
        goto cleanup;
    }

    if (opts->verbose) {
        fprintf(stderr, "[taurus] Canonicalizing (C14N %s)...\n",
                opts->version == 0 ? "1.0" : "1.1");
    }

    /* Canonicalize */
    canonical = taurus_c14n_canonicalize(doc, opts->version, 0);
    if (!canonical) {
        cli_error("canonicalization failed");
        result = CLI_ERROR_INTERNAL;
        goto cleanup;
    }

    /* Open output file if specified */
    if (opts->output_file) {
        out = fopen(opts->output_file, "wb");
        if (!out) {
            cli_error("cannot open output file: %s (%s)",
                      opts->output_file, strerror(errno));
            result = CLI_ERROR_IO;
            goto cleanup;
        }
        close_output = 1;
        if (opts->verbose) {
            fprintf(stderr, "[taurus] Output: %s\n", opts->output_file);
        }
    }

    /* Write canonical output */
    fprintf(out, "%s", canonical);

cleanup:
    if (close_output && out) fclose(out);
    if (canonical) taurus_free_string(canonical);
    if (doc) taurus_document_free(doc);
    if (xml_content) free(xml_content);
    if (opts) c14n_options_free(opts);

    return result;
}

static void c14n_print_help(void) {
    printf("Usage: taurus c14n [OPTIONS] FILE\n");
    printf("\n");
    printf("Canonicalize XML document (C14N) for digital signatures.\n");
    printf("\n");
    printf("C14N produces a canonical form of XML where semantically\n");
    printf("equivalent documents produce identical byte sequences.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  FILE                Input XML file (use - for stdin)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o, --output FILE   Output file (default: stdout)\n");
    printf("  --version VERSION   C14N version: 1.0 or 1.1 (default: 1.0)\n");
    printf("  -v, --verbose       Show verbose output\n");
    printf("  -q, --quiet         Suppress warnings\n");
    printf("  -h, --help          Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  taurus c14n doc.xml                    # C14N 1.0 to stdout\n");
    printf("  taurus c14n --version 1.1 doc.xml      # C14N 1.1\n");
    printf("  taurus c14n -o canonical.xml doc.xml   # Write to file\n");
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

static cli_command_t c14n_command = {
    .name = "c14n",
    .description = "Canonicalize XML (C14N) for digital signatures",
    .execute = c14n_execute,
    .print_help = c14n_print_help
};

cli_command_t* cli_command_c14n(void) {
    return &c14n_command;
}
