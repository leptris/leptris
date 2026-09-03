/**
 * @file xquery.c
 * @brief XQuery command implementation (TODO.xslt-full/11)
 *
 * Command: leptris xquery [-s FILE] -q FILE (or '-' for stdin, or
 * an inline expression with -e). Prints the result's string form:
 * FLWOR sequences join space-separated (value-of rule).
 */

#include "../cli.h"
#include "../options.h"
#include "../error.h"
#include "../src/include/leptris.h"
#include "leptris/xquery/xquery.h"
#include "leptris/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_stream(FILE* fp, size_t* out_len) {
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char* grown = (char*)realloc(buf, cap);
            if (!grown) { free(buf); return NULL; }
            buf = grown;
            continue;
        }
        break;
    }
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

static char* read_file(const char* path, size_t* out_len) {
    if (strcmp(path, "-") == 0) return read_stream(stdin, out_len);
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    char* s = read_stream(fp, out_len);
    fclose(fp);
    return s;
}

static void xquery_print_help(void);

typedef struct {
    const char* source_file;
    const char* query_file;
    const char* query_expr;
} xquery_options_t;

static cli_result_t xquery_run(int argc, char** argv) {
    xquery_options_t opts = {0};
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                cli_error("-s requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts.source_file = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-q") == 0) {
            if (i + 1 >= argc) {
                cli_error("-q requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts.query_file = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                cli_error("-e requires an argument");
                return CLI_ERROR_ARGS;
            }
            opts.query_expr = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            xquery_print_help();
            return CLI_SUCCESS;
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            cli_error("unknown option: %s", argv[i]);
            return CLI_ERROR_ARGS;
        } else {
            /* Positional: the query file. */
            if (!opts.query_file) opts.query_file = argv[i];
            else {
                cli_error("too many arguments");
                return CLI_ERROR_ARGS;
            }
            i++;
        }
    }
    if (!opts.query_file && !opts.query_expr) {
        cli_error("missing query: use -q FILE or -e EXPR");
        return CLI_ERROR_ARGS;
    }

    char* query = NULL;
    size_t query_len = 0;
    if (opts.query_file) {
        query = read_file(opts.query_file, &query_len);
        if (!query) {
            cli_error("cannot read query file: %s", opts.query_file);
            return CLI_ERROR_IO;
        }
    } else {
        query_len = strlen(opts.query_expr);
        query = (char*)malloc(query_len + 1);
        if (query) memcpy(query, opts.query_expr, query_len + 1);
    }
    if (!query) return CLI_ERROR_MEMORY;

    LeptrisXQuery xq = leptris_xquery_parse(query, query_len);
    free(query);
    if (!xq) {
        cli_error("XQuery parse failed: %s", leptris_last_error());
        return CLI_ERROR_PARSE;
    }

    cli_result_t rc = CLI_SUCCESS;
    LeptrisDocument doc = NULL;
    if (opts.source_file) {
        doc = leptris_parse_file(opts.source_file, NULL);
        if (!doc) {
            cli_error("cannot parse source document: %s",
                      opts.source_file);
            leptris_xquery_free(xq);
            return CLI_ERROR_PARSE;
        }
    } else {
        /* Context-free queries still need a context node: an empty
         * document stands in. */
        doc = leptris_parse_string("<_/>", 4, NULL);
        if (!doc) {
            leptris_xquery_free(xq);
            return CLI_ERROR_PARSE;
        }
    }

    LeptrisXPathResult r = leptris_xquery_eval(xq, doc, NULL);
    if (!r) {
        cli_error("XQuery evaluation failed");
        rc = CLI_ERROR_XPATH;
    } else if (leptris_xpath_result_type(r) == LEPTRIS_XPATH_NODESET) {
        size_t n = leptris_xpath_result_count(r);
        for (size_t k = 0; k < n; k++) {
            const char* v = leptris_xpath_result_node_value(r, k);
            if (k) putchar(' ');
            fputs(v ? v : "", stdout);
        }
        if (n) putchar('\n');
    } else {
        char* s = leptris_xpath_result_string(r);
        printf("%s\n", s ? s : "");
        leptris_free_string(s);
    }
    if (r) leptris_xpath_result_free(r);
    leptris_document_free(doc);
    leptris_xquery_free(xq);
    return rc;
}

static void xquery_print_help(void) {
    printf("Usage: leptris xquery [OPTIONS] (-q FILE | -e EXPR)\n\n");
    printf("Execute an XQuery 1.0 query.\n\n");
    printf("Options:\n");
    printf("  -s FILE   source document\n");
    printf("  -q FILE   query file ('-' for stdin)\n");
    printf("  -e EXPR   inline query expression\n");
}

static cli_command_t xquery_command = {
    .name = "xquery",
    .description = "Execute an XQuery 1.0 query",
    .execute = xquery_run,
    .print_help = xquery_print_help
};

cli_command_t* cli_command_xquery(void) {
    return &xquery_command;
}
