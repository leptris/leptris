/**
 * @file xslt.c
 * @brief XSLT command implementation
 *
 * Command: leptris xslt -s STYLESHEET [INPUT]. INPUT is a file
 * path or '-' for stdin; absent means stdin (the xsltproc
 * convention). Prints the serialized result tree to stdout.
 */

#include "../cli.h"
#include "../options.h"
#include "../error.h"
#include "../src/include/leptris.h"
#include "leptris/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xslt_print_help(void);

typedef struct {
    const char* stylesheet;
    const char* input;
} xslt_options_t;

static LeptrisDocument load_input(const char* input) {
    if (input && strcmp(input, "-") != 0)
        return leptris_parse_file(input, NULL);
    size_t len = 0;
    char* xml = cli_read_file("-", &len);
    if (!xml) return NULL;
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    free(xml);
    return doc;
}

static cli_result_t xslt_run(int argc, char** argv) {
    xslt_options_t opts = {0};
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                cli_error("-s requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts.stylesheet = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            xslt_print_help();
            return CLI_SUCCESS;
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            cli_error("unknown option: %s", argv[i]);
            return CLI_ERROR_ARGS;
        } else {
            if (!opts.input) opts.input = argv[i];
            else {
                cli_error("too many arguments");
                return CLI_ERROR_ARGS;
            }
            i++;
        }
    }
    if (!opts.stylesheet) {
        cli_error("missing stylesheet: use -s FILE");
        return CLI_ERROR_ARGS;
    }

    LeptrisXslt sheet = leptris_xslt_parse_file(opts.stylesheet);
    if (!sheet) {
        cli_error("stylesheet compilation failed: %s: %s",
                  opts.stylesheet, leptris_last_error());
        return CLI_ERROR_PARSE;
    }

    LeptrisDocument doc = load_input(opts.input);
    if (!doc) {
        cli_error("cannot parse input document: %s: %s",
                  opts.input ? opts.input : "(empty)",
                  leptris_last_error());
        leptris_xslt_free(sheet);
        return CLI_ERROR_PARSE;
    }

    char* out = leptris_xslt_apply_string(sheet, doc);
    leptris_document_free(doc);
    leptris_xslt_free(sheet);
    if (!out) {
        cli_error("XSLT transformation failed: %s",
                  leptris_last_error());
        return CLI_ERROR_XSLT;
    }
    fputs(out, stdout);
    size_t n = strlen(out);
    if (n == 0 || out[n - 1] != '\n') putchar('\n');
    leptris_free_string(out);
    return CLI_SUCCESS;
}

static void xslt_print_help(void) {
    printf("Usage: leptris xslt -s STYLESHEET [INPUT]\n\n");
    printf("Apply an XSLT stylesheet to an XML document.\n\n");
    printf("Options:\n");
    printf("  -s FILE   stylesheet (required)\n");
    printf("  INPUT     input document: file, '-' for stdin, or absent\n");
    printf("            to read stdin (the xsltproc convention)\n");
}

static cli_command_t xslt_command = {
    .name = "xslt",
    .description = "Apply an XSLT stylesheet to an XML document",
    .execute = xslt_run,
    .print_help = xslt_print_help
};

cli_command_t* cli_command_xslt(void) {
    return &xslt_command;
}
