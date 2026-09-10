/**
 * @file validate.c
 * @brief Validate command: one-stop RELAX NG + Schematron (lane 16.6)
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

static char* read_file(const char* path, size_t* out_len) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0) {
        fclose(fp);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = '\0';
    *out_len = (size_t)n;
    return buf;
}

static const char* local_part(const char* n) {
    const char* c = n ? strchr(n, ':') : NULL;
    return c ? c + 1 : n;
}

/* Text content of an element: element_text, falling back to the
 * concatenated text-node children. */
static void print_element_text(LeptrisElement e) {
    const char* t = leptris_element_text(e);
    if (t) {
        fputs(t, stdout);
        return;
    }
    for (LeptrisNodeRef c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        const char* txt = leptris_text_node_get_content(c);
        if (txt) fputs(txt, stdout);
    }
}

static int run_schematron(const char* path, const char* phase,
                          int want_svrl, LeptrisDocument doc,
                          int* invalid) {
    size_t len = 0;
    char* sch = read_file(path, &len);
    if (!sch) {
        cli_error("cannot read schema: %s", path);
        return CLI_ERROR_IO;
    }
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisSchematron s = leptris_schematron_parse_phase(
        sch, len, phase && phase[0] ? phase : NULL, &st);
    free(sch);
    if (!s) {
        cli_error("schematron parse failed: %s",
                  leptris_status_string(st));
        return CLI_ERROR_IO;
    }
    LeptrisDocument svrl = leptris_schematron_validate(s, doc);
    if (!svrl) {
        cli_error("schematron validation failed");
        leptris_schematron_free(s);
        return CLI_ERROR_INTERNAL;
    }
    if (want_svrl) {
        char* out = leptris_document_serialize(svrl, NULL);
        if (out) {
            fputs(out, stdout);
            leptris_free_string(out);
        }
    } else {
        int failed = 0, reports = 0;
        LeptrisElement root = leptris_document_root(svrl);
        for (LeptrisElement c = leptris_element_first_child_any(root);
             c;
             c = leptris_element_next_sibling_any(c)) {
            const char* n =
                local_part(leptris_element_name(c));
            if (strcmp(n, "failed-assert") == 0) {
                failed++;
                const char* loc =
                    leptris_element_attribute(c, "location");
                printf("failed-assert at %s: ",
                       loc ? loc : "?");
                for (LeptrisElement t =
                         leptris_element_first_child_any(c);
                     t;
                     t = leptris_element_next_sibling_any(t)) {
                    if (strcmp(local_part(leptris_element_name(t)),
                               "text") == 0)
                        print_element_text(t);
                }
                printf("\n");
            } else if (strcmp(n, "successful-report") == 0) {
                reports++;
            }
        }
        if (failed == 0 && reports == 0)
            printf("schematron: valid\n");
        else if (failed == 0)
            printf("schematron: valid (%d report(s))\n", reports);
    }
    if (!leptris_schematron_valid(s, doc)) *invalid = 1;
    leptris_document_free(svrl);
    leptris_schematron_free(s);
    return -1; /* continue */
}

static cli_result_t validate_execute(int argc, char** argv) {
    const char* rng_path = NULL;
    const char* sch_path = NULL;
    const char* phase = NULL;
    const char* xml_path = NULL;
    int want_svrl = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rng") == 0 && i + 1 < argc) {
            rng_path = argv[++i];
        } else if ((strcmp(argv[i], "--schematron") == 0 ||
                    strcmp(argv[i], "-s") == 0) &&
                   i + 1 < argc) {
            sch_path = argv[++i];
        } else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc) {
            phase = argv[++i];
        } else if (strcmp(argv[i], "--svrl") == 0) {
            want_svrl = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            printf("Usage: leptris validate [OPTIONS] XML\n");
            printf("\n");
            printf("Validate an XML document against RELAX NG");
            printf(" and/or ISO Schematron schemas.\n");
            printf("\n");
            printf("Options:\n");
            printf("  --rng FILE          RELAX NG schema\n");
            printf("  --schematron FILE   ISO Schematron schema\n");
            printf("  --phase ID          Schematron phase to");
            printf(" select\n");
            printf("  --svrl              Print the SVRL report");
            printf(" instead of the summary\n");
            printf("  -h, --help          Show this help\n");
            printf("\n");
            printf("Exit status: 0 valid, 1 invalid, 3 I/O or");
            printf(" schema error.\n");
            return CLI_SUCCESS;
        } else if (!xml_path) {
            xml_path = argv[i];
        } else {
            cli_error("too many arguments");
            return CLI_ERROR_ARGS;
        }
    }
    if (!xml_path) {
        cli_error("validate requires an XML document");
        return CLI_ERROR_ARGS;
    }
    if (!rng_path && !sch_path) {
        cli_error("validate needs --rng and/or --schematron");
        return CLI_ERROR_ARGS;
    }
    LeptrisDocument doc = read_doc(xml_path);
    if (!doc) {
        cli_error("cannot read or parse: %s", xml_path);
        return CLI_ERROR_IO;
    }
    int invalid = 0;
    if (rng_path) {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse_file(rng_path, &st);
        if (!rng) {
            cli_error("RELAX NG parse failed: %s",
                      leptris_status_string(st));
            leptris_document_free(doc);
            return CLI_ERROR_IO;
        }
        if (leptris_rng_validate(rng, doc)) {
            printf("rng: valid\n");
        } else {
            printf("rng: invalid\n");
            const char* e = leptris_rng_error(rng);
            if (e && e[0]) printf("  %s\n", e);
            invalid = 1;
        }
        leptris_rng_free(rng);
    }
    if (sch_path) {
        cli_result_t r = run_schematron(
            sch_path, phase, want_svrl, doc, &invalid);
        if (r != -1) {
            leptris_document_free(doc);
            return r;
        }
    }
    leptris_document_free(doc);
    if (invalid) return CLI_ERROR_PARSE;
    return CLI_SUCCESS;
}

static cli_command_t validate_command = {
    .name = "validate",
    .description = "Validate XML against RELAX NG / Schematron",
    .execute = validate_execute,
    .print_help = NULL
};

cli_command_t* cli_command_validate(void) {
    return &validate_command;
}
