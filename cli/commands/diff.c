/**
 * @file diff.c
 * @brief Diff command: structural XML diff (lane 17)
 */

#include "leptris.h"
#include "../cli.h"
#include "../error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static LeptrisDocument read_doc(const char* path) {
    FILE* fp = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!fp) return NULL;
    char* buf = NULL;
    size_t len = 0, cap = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            cap = cap ? cap * 2 : 8192;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) {
                free(buf);
                if (fp != stdin) fclose(fp);
                return NULL;
            }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    if (fp != stdin) fclose(fp);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d =
        buf ? leptris_parse_string(buf, len, &st) : NULL;
    free(buf);
    return d;
}

static cli_result_t diff_execute(int argc, char** argv) {
    int ignore_ws = 0;
    const char* paths[2] = {NULL, NULL};
    int np = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ignore-ws") == 0) {
            ignore_ws = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            printf("Usage: leptris diff [OPTIONS] A B\n");
            printf("\n");
            printf("Structural XML diff between two documents");
            printf(" (lane 17).\n");
            printf("\n");
            printf("Options:\n");
            printf("  --ignore-ws        Ignore whitespace-only");
            printf(" text nodes\n");
            printf("  -h, --help         Show this help\n");
            return CLI_SUCCESS;
        } else if (np < 2) {
            paths[np++] = argv[i];
        } else {
            cli_error("too many arguments");
            return CLI_ERROR_ARGS;
        }
    }
    if (np != 2) {
        cli_error("diff requires two documents (paths or '-')");
        return CLI_ERROR_ARGS;
    }
    LeptrisDocument a = read_doc(paths[0]);
    LeptrisDocument b = read_doc(paths[1]);
    if (!a) {
        cli_error("cannot read or parse: %s", paths[0]);
        if (b) leptris_document_free(b);
        return CLI_ERROR_IO;
    }
    if (!b) {
        cli_error("cannot read or parse: %s", paths[1]);
        leptris_document_free(a);
        return CLI_ERROR_IO;
    }
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDiff d = leptris_diff(
        a, b, ignore_ws ? LEPTRIS_DIFF_IGNORE_WS_TEXT
                        : LEPTRIS_DIFF_DEFAULT,
        &st);
    if (!d) {
        cli_error("diff failed: %s", leptris_status_string(st));
        leptris_document_free(a);
        leptris_document_free(b);
        return CLI_ERROR_INTERNAL;
    }
    if (leptris_diff_op_count(d) == 0) {
        printf("documents are identical\n");
    } else {
        char* s = leptris_diff_serialize(d);
        if (s) {
            fputs(s, stdout);
            leptris_free_string(s);
        }
    }
    leptris_diff_free(d);
    leptris_document_free(a);
    leptris_document_free(b);
    return CLI_SUCCESS;
}

static cli_command_t diff_command = {
    .name = "diff",
    .description = "Structural XML diff between two documents",
    .execute = diff_execute,
    .print_help = NULL
};

cli_command_t* cli_command_diff(void) {
    return &diff_command;
}
