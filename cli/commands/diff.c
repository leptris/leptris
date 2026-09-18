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

static const char* json_escape(const char* s, char* buf, size_t bufsz) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)(s ? s : "");
         *p && o + 7 < bufsz; p++) {
        unsigned char ch = *p;
        if (ch == '"' || ch == '\\') {
            buf[o++] = '\\';
            buf[o++] = (char)ch;
        } else if (ch == '\n') {
            buf[o++] = '\\'; buf[o++] = 'n';
        } else if (ch == '\t') {
            buf[o++] = '\\'; buf[o++] = 't';
        } else if (ch == '\r') {
            buf[o++] = '\\'; buf[o++] = 'r';
        } else if (ch < 0x20) {
            o += (size_t)snprintf(buf + o, bufsz - o, "\\u%04x", ch);
        } else {
            buf[o++] = (char)ch;
        }
    }
    buf[o] = 0;
    return buf;
}

static const char* op_kind_name(LeptrisDiffOpType t) {
    switch (t) {
        case LEPTRIS_DIFF_INSERT:      return "insert";
        case LEPTRIS_DIFF_DELETE:      return "delete";
        case LEPTRIS_DIFF_UPDATE_TEXT: return "update-text";
        case LEPTRIS_DIFF_UPDATE_ATTR: return "update-attr";
        default:                       return "unknown";
    }
}

static cli_result_t diff_execute(int argc, char** argv) {
    int ignore_ws = 0;
    int want_summary = 0;
    int want_json = 0;
    const char* paths[2] = {NULL, NULL};
    int np = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ignore-ws") == 0) {
            ignore_ws = 1;
        } else if (strcmp(argv[i], "--summary") == 0) {
            want_summary = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            want_json = 1;
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
            printf("  --summary          Print per-kind op counts");
            printf(" instead of the op list\n");
            printf("  --json             Print the op list as JSON\n");
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
    size_t nops = leptris_diff_op_count(d);
    if (want_json) {
        printf("[");
        for (size_t i = 0; i < nops; i++) {
            char eb[4096], pb[512], nb[256];
            printf("%s\n  { \"op\": \"%s\", \"path\": \"%s\"",
                   i ? "," : "",
                   op_kind_name(leptris_diff_op_type(d, i)),
                   json_escape(leptris_diff_op_path(d, i), pb,
                               sizeof(pb)));
            if (leptris_diff_op_type(d, i) == LEPTRIS_DIFF_UPDATE_ATTR)
                printf(", \"name\": \"%s\"",
                       json_escape(leptris_diff_op_name(d, i), nb,
                                   sizeof(nb)));
            printf(", \"before\": \"%s\", \"after\": \"%s\" }",
                   json_escape(leptris_diff_op_before(d, i), eb,
                               sizeof(eb)),
                   json_escape(leptris_diff_op_after(d, i), pb,
                               sizeof(pb)));
        }
        printf("%s]\n", nops ? "\n" : "");
    } else if (want_summary) {
        if (nops == 0) {
            printf("documents are identical\n");
        } else {
            size_t ins = 0, del = 0, utxt = 0, uattr = 0;
            for (size_t i = 0; i < nops; i++) {
                switch (leptris_diff_op_type(d, i)) {
                    case LEPTRIS_DIFF_INSERT:      ins++;  break;
                    case LEPTRIS_DIFF_DELETE:      del++;  break;
                    case LEPTRIS_DIFF_UPDATE_TEXT: utxt++; break;
                    case LEPTRIS_DIFF_UPDATE_ATTR: uattr++; break;
                    default: break;
                }
            }
            if (uattr)
                printf("%zu attribute%s changed\n", uattr,
                       uattr == 1 ? "" : "s");
            if (utxt)
                printf("%zu text change%s\n", utxt,
                       utxt == 1 ? "" : "s");
            if (ins)
                printf("%zu element%s inserted\n", ins,
                       ins == 1 ? "" : "s");
            if (del)
                printf("%zu element%s deleted\n", del,
                       del == 1 ? "" : "s");
        }
    } else if (nops == 0) {
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
