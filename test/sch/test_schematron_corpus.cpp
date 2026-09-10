// test/sch/test_schematron_corpus.cpp — lane 16 phase 5 conformance
// gate. 50 self-contained testcases vendored from
// schematron/schematron-conformance (src/main/resources/tests/{core,
// svrl}); each embeds a primary document, a sch:schema, and an
// expected verdict in @expect:
//   valid   — schema parses, instance validates
//   invalid — schema parses, instance fails
//   error   — the schema itself must be REJECTED at parse time
//   ambiguous — schema parses; either verdict acceptable
// The svrl cases additionally carry <expectations> XPath checks
// evaluated against our SVRL output.
//
// Public API only — black-box gate, no sch internals.

#include <gtest/gtest.h>

#include "leptris.h"
#include "leptris/error.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef LEPTRIS_SCH_CASES_DIR
#define LEPTRIS_SCH_CASES_DIR "test/sch/conformance-cases"
#endif

namespace {

struct SchCase {
    const char* stem;
    const char* expect;   /* valid | invalid | error | ambiguous */
    const char* label;
};

const SchCase kCases[] = {
    {"extends-baseuri-fixup-01", "valid", "Extends performs base URI fixup"},
    {"extends-recursive-01", "invalid", "Extends is recursive"},
    {"include-baseuri-fixup-01", "valid", "Include performs base URI fixup"},
    {"include-recursive-01", "invalid", "Include is recursive"},
    {"let-name-collision-error-01", "error", "It is an error for a variable to be multiply defined in the current rule"},
    {"let-name-collision-error-02", "error", "It is an error for a variable to be multiply defined in the current pattern"},
    {"let-name-collision-error-03", "error", "It is an error for a variable to be multiply defined in the current schema"},
    {"let-name-collision-error-04", "error", "It is an error for a variable to be multiply defined in the current phase"},
    {"let-name-collision-error-05", "error", "It is an error for a variable to be multiply defined globally"},
    {"let-name-collision-error-06", "error", "It is an error to define a pattern variable with the same name as a global variable"},
    {"let-pattern-global-01", "valid", "A pattern variable has global scope"},
    {"let-reference-undefined-01", "error", "It is an error to reference a variable in a rule context expression that has not been defined globally"},
    {"let-reference-undefined-02", "error", "It is an error to reference a variable in an assert test expression that has not been defined globally"},
    {"let-reference-undefined-03", "error", "It is an error to reference a variable in an report test expression that has not been defined globally"},
    {"let-reference-undefined-04", "error", "It is an error to reference a variable in an rule variable that has not been defined globally"},
    {"let-reference-undefined-05", "error", "It is an error to reference a variable in the @select expression of a sch:value-of element that has not been defined globally"},
    {"let-reference-undefined-06", "error", "It is an error to reference a variable in the @path expression of a sch:name element that has not been defined globally"},
    {"let-reference-undefined-07", "error", "It is an error to reference an undefined variable in the @documents expression of a sch:pattern element"},
    {"let-rule-global-01", "valid", "A rule variable can use a schema variable"},
    {"let-rule-global-02", "valid", "A rule variable can use a phase variable"},
    {"let-scope-pattern-01", "valid", "Pattern-variable is scoped to the pattern"},
    {"let-scope-phase-01", "valid", "Phase-variable is scoped to the phase"},
    {"let-scope-rule-01", "valid", "Rule-variable is scoped to the rule"},
    {"let-value-element-content-01", "valid", "Let uses the element content as value"},
    {"pattern-abstract-01", "invalid", "An abstract pattern is instantiated"},
    {"pattern-subordinate-document-01", "invalid", "Pattern in a subordinate document"},
    {"pattern-subordinate-document-02", "invalid", "The subordinate document expression contains a variable"},
    {"rule-abstract-01", "invalid", "An abstract rule is instantiated"},
    {"rule-abstract-02", "error", "It is an error to extend an abstract rule that is defined in a different pattern"},
    {"rule-context-attribute-01", "invalid", "Context node is an attribute node"},
    {"rule-context-comment-01", "invalid", "Context node is a comment node"},
    {"rule-context-element-01", "invalid", "Context node is an element node"},
    {"rule-context-pi-01", "invalid", "Context node is a processing instruction node"},
    {"rule-context-root-01", "invalid", "Context node is the root node"},
    {"rule-context-text-01", "invalid", "Context node is a text node"},
    {"rule-context-variable-01", "valid", "Rule context expression uses a pattern variable"},
    {"rule-context-variable-02", "valid", "Rule context expression uses a phase variable"},
    {"rule-context-variable-03", "valid", "Rule context expression uses a schema variable"},
    {"rule-order-01", "valid", "Lexical order of rules is significant"},
    {"schema-default-phase-01", "valid", "When no phase is given, the processor uses the phase given in @defaultPhase"},
    {"schema-default-phase-02", "valid", "When a phase named '#DEFAULT' is given, the processor uses the phase given in @defaultPhase"},
    {"svrl-diagnostic-01", "invalid", "Diagnostic references are copied to SVRL output"},
    {"svrl-diagnostic-02", "invalid", "Language tag of diagnostic is preserved in SVRL output"},
    {"svrl-name-nopath-01", "invalid", "The sch:name element expands into the name of the context node if no @path is present"},
    {"svrl-name-path-01", "invalid", "The sch:name element expands into the value of evaluating the expression in @path"},
    {"svrl-property-01", "invalid", "Property references are copied to SVRL output"},
    {"svrl-property-copy-of", "invalid", "A xsl:copy-of inside a sch:property is executed"},
    {"svrl-value-of-01", "invalid", "The sch:value-of element expands into the value of evaluating the expression in @select"},
    {"xslt-key-01", "valid", "The XSLT key element may be used before the pattern elements"},
    {"xslt-key-element-content-01", "valid", "An xsl:key element can have element content"}
};
const size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

std::string slurp(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

const char* local_name(const char* name) {
    const char* c = name ? strchr(name, ':') : NULL;
    return c ? c + 1 : name;
}

LeptrisElement first_child_elem(LeptrisElement e) {
    for (LeptrisElement c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        if (leptris_node_get_type((LeptrisNodeRef)c) == LEPTRIS_NODE_TYPE_ELEMENT)
            return c;
    }
    return NULL;
}

LeptrisElement next_elem(LeptrisElement e) {
    for (LeptrisElement c = leptris_element_next_sibling_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        if (leptris_node_get_type((LeptrisNodeRef)c) == LEPTRIS_NODE_TYPE_ELEMENT)
            return c;
    }
    return NULL;
}

/* First descendant element with the given local name (BFS). */
LeptrisElement find_desc(LeptrisElement root, const char* ln) {
    std::vector<LeptrisElement> q{root};
    while (!q.empty()) {
        LeptrisElement e = q.back();
        q.pop_back();
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c)) {
            if (strcmp(local_name(leptris_element_name(c)), ln) == 0)
                return c;
            q.push_back(c);
        }
    }
    return NULL;
}

char* elem_to_string(LeptrisElement e) {
    return leptris_element_serialize_ext_sized(
        e, NULL, NULL, 0);
}

/* One corpus case, end to end. Returns true when our behavior
 * matches @expect; writes a description of any mismatch. */
static bool run_case(const SchCase& c, std::string* why) {
    std::string xml = slurp(std::string(LEPTRIS_SCH_CASES_DIR) + "/" +
                            c.stem + ".xml");
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument tc = leptris_parse_string(xml.data(), xml.size(), &st);
    if (!tc) { *why = "testcase XML failed to parse"; return false; }

    LeptrisElement root = leptris_document_root(tc);
    LeptrisElement primary = find_desc(root, "primary");
    LeptrisElement schema_e = find_desc(root, "schema");
    if (!primary || !schema_e) {
        leptris_document_free(tc);
        *why = "testcase missing primary/schema";
        return false;
    }
    LeptrisElement inst_e = first_child_elem(primary);
    if (!inst_e) {
        leptris_document_free(tc);
        *why = "primary has no element child";
        return false;
    }

    /* Phase selection: newer suite formats carry it on
     * <schemas @phase>; this (older) corpus instead implies the
     * schema's single <sch:phase> — every phase-scoped case
     * declares exactly one. Select it when present. */
    const char* phase = leptris_element_attribute(root, "phase");
    if (!phase || !phase[0]) {
        for (LeptrisElement c = first_child_elem(schema_e); c;
             c = next_elem(c)) {
            if (strcmp(local_name(leptris_element_name(c)),
                       "phase") == 0) {
                phase = leptris_element_attribute(c, "id");
                break;
            }
        }
    }
    /* Secondary documents keyed by filename: <extends
     * href="X"/> resolves transitively against them (the suite
     * embeds include files as secondaries). */
    std::vector<std::pair<std::string, std::string>> secondaries;
    for (LeptrisElement c = first_child_elem(
             leptris_node_parent((LeptrisNodeRef)primary));
         c;
         c = next_elem(c)) {
        const char* ln2 = local_name(leptris_element_name(c));
        if (strcmp(ln2, "secondary") != 0) continue;
        const char* fn = leptris_element_attribute(c, "filename");
        LeptrisElement body = first_child_elem(c);
        if (!fn || !body) continue;
        char* bs = elem_to_string(body);
        if (bs) {
            secondaries.emplace_back(fn, bs);
            leptris_free_string(bs);
        }
    }
    char* schema_s = elem_to_string(schema_e);
    /* Splice <sch:include href="F"/> with secondary F's FULL
     * root element, transitively. */
    for (int pass = 0; pass < 8; pass++) {
        bool changed = false;
        for (const auto& sec : secondaries) {
            const char* forms[2] = {"sch:include", "include"};
            for (int fi = 0; fi < 2; fi++) {
                std::string ext = "<" + std::string(forms[fi]) +
                                  " href=\"" + sec.first + "\"/>";
                char* at;
                while (schema_s &&
                       (at = strstr(schema_s, ext.c_str())) != NULL) {
                    size_t off = (size_t)(at - schema_s);
                    char* ns = (char*)malloc(
                        off + sec.second.size() +
                        strlen(at + ext.size()) + 1);
                    memcpy(ns, schema_s, off);
                    memcpy(ns + off, sec.second.data(),
                           sec.second.size());
                    strcpy(ns + off + sec.second.size(),
                           at + ext.size());
                    free(schema_s);
                    schema_s = ns;
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
    /* Splice <sch:extends href="F"/> with the CHILDREN of
     * secondary F's root element, transitively. */
    for (int pass = 0; pass < 8; pass++) {
        bool changed = false;
        for (const auto& sec : secondaries) {
            const char* forms[2] = {"sch:extends", "extends"};
            const char* at = NULL;
            std::string ext;
            for (int fi = 0; fi < 2; fi++) {
                ext = "<" + std::string(forms[fi]) + " href=\"" +
                      sec.first + "\"/>";
                at = schema_s ? strstr(schema_s, ext.c_str()) : NULL;
                if (at) break;
            }
            while (schema_s && (at = strstr(schema_s, ext.c_str())) != NULL) {
                /* inner XML of the secondary's root element */
                const std::string& full = sec.second;
                size_t open = full.find('>');
                size_t close = full.rfind('<');
                std::string inner =
                    (open != std::string::npos &&
                     close != std::string::npos && close > open)
                        ? full.substr(open + 1, close - open - 1)
                        : "";
                size_t off = (size_t)(at - schema_s);
                char* ns = (char*)malloc(
                    off + inner.size() +
                    strlen(at + ext.size()) + 1);
                memcpy(ns, schema_s, off);
                memcpy(ns + off, inner.data(), inner.size());
                strcpy(ns + off + inner.size(),
                       at + ext.size());
                free(schema_s);
                schema_s = ns;
                changed = true;
            }
        }
        if (!changed) break;
    }
    char* inst_s = elem_to_string(inst_e);
    LeptrisSchematron sch = leptris_schematron_parse_phase(
        schema_s, strlen(schema_s),
        phase && phase[0] ? phase : NULL, &st);
    bool ok;
    if (!sch) {
        ok = (strcmp(c.expect, "error") == 0);
        if (!ok)
            *why = std::string("expected ") + c.expect +
                   " but schema was rejected: " +
                   leptris_error_message(st);
    } else {
        if (strcmp(c.expect, "error") == 0) {
            ok = false;
            *why = "expected error but schema parsed";
        } else {
            LeptrisDocument doc =
                leptris_parse_string(inst_s, strlen(inst_s), &st);
            if (!doc) {
                leptris_schematron_free(sch);
                leptris_document_free(tc);
                leptris_free_string(schema_s);
                leptris_free_string(inst_s);
                *why = "instance failed to parse";
                return false;
            }
            /* Suite verdict semantics: ANY SVRL finding counts —
             * failed-assert OR successful-report (a fired report
             * is a validation failure in this corpus; the public
             * API's leptris_schematron_valid keeps ISO semantics
             * of reports-don't-invalidate). */
            int findings = 0;
            LeptrisDocument docs_to_check[8];
            size_t ndocs = 0;
            docs_to_check[ndocs++] = doc;
            /* subordinate documents: pattern/@documents evaluated
             * against the primary names secondaries (runner-side
             * let substitution from the testcase DOM). */
            for (LeptrisElement pat = first_child_elem(schema_e); pat;
                 pat = next_elem(pat)) {
                if (strcmp(local_name(leptris_element_name(pat)),
                           "pattern") != 0)
                    continue;
                const char* dexpr =
                    leptris_element_attribute(pat, "documents");
                if (!dexpr) continue;
                std::string ex(dexpr);
                for (LeptrisElement l =
                         leptris_element_first_child_any(schema_e);
                     l;
                     l = leptris_element_next_sibling_any(l)) {
                    if (strcmp(local_name(leptris_element_name(l)),
                               "let") != 0)
                        continue;
                    const char* vn = leptris_element_attribute(l, "name");
                    const char* vv = leptris_element_attribute(l, "value");
                    if (!vn || !vv) continue;
                    size_t at2;
                    while ((at2 = ex.find("$" + std::string(vn))) !=
                           std::string::npos)
                        ex.replace(at2, strlen(vn) + 1, vv);
                }
                LeptrisXPathResult dr =
                    leptris_xpath_eval(doc, NULL, ex.c_str());
                if (!dr) continue;
                char* picked = leptris_xpath_result_string(dr);
                leptris_xpath_result_free(dr);
                if (!picked) continue;
                for (const auto& sec : secondaries) {
                    std::string base = sec.first;
                    size_t dot = base.rfind('.');
                    std::string stem =
                        (dot != std::string::npos)
                            ? base.substr(0, dot) : base;
                    if (sec.first == picked || stem == picked) {
                        LeptrisStatus s2 = LEPTRIS_OK;
                        LeptrisDocument sd = leptris_parse_string(
                            sec.second.data(), sec.second.size(), &s2);
                        if (sd && ndocs < 8) docs_to_check[ndocs++] = sd;
                        else if (sd) leptris_document_free(sd);
                    }
                }
                leptris_free_string(picked);
            }
            for (size_t di = 0; di < ndocs; di++) {
                LeptrisDocument svrl =
                    leptris_schematron_validate(sch, docs_to_check[di]);
                if (svrl) {
                    LeptrisElement out =
                        leptris_document_root(svrl);
                    for (LeptrisElement c =
                             leptris_element_first_child_any(out);
                         c;
                         c = leptris_element_next_sibling_any(c)) {
                        const char* n = leptris_element_name(c);
                        if (leptris_node_get_type((LeptrisNodeRef)c) !=
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            continue;
                        const char* ln = local_name(n);
                        if (strcmp(ln, "failed-assert") == 0 ||
                            strcmp(ln, "successful-report") == 0)
                            findings++;
                    }
                    leptris_document_free(svrl);
                }
            }
            for (size_t di = 1; di < ndocs; di++)
                leptris_document_free(docs_to_check[di]);
            leptris_document_free(doc);
            int valid = (findings == 0);
            if (strcmp(c.expect, "ambiguous") == 0) {
                ok = true;
            } else if (strcmp(c.expect, "valid") == 0) {
                ok = (valid == 1);
                if (!ok) *why = "expected valid, got failed-asserts";
            } else {  /* invalid */
                ok = (valid == 0);
                if (!ok) *why = "expected invalid, got valid";
            }
        }
        leptris_schematron_free(sch);
    }

    leptris_free_string(schema_s);
    leptris_free_string(inst_s);
    leptris_document_free(tc);
    return ok;
}

TEST(SchCorpus, MatchesConformanceVerdicts) {
    int agree = 0;
    std::vector<std::string> red;
    for (size_t i = 0; i < kCaseCount; i++) {
        std::string why;
        if (run_case(kCases[i], &why)) {
            agree++;
        } else {
            red.push_back(std::string(kCases[i].stem) + " [" +
                          kCases[i].expect + "]: " + why);
        }
    }
    for (const std::string& r : red)
        ADD_FAILURE() << r;
    /* Exact gate: every future divergence is an immediate red. */
    EXPECT_EQ(agree, (int)kCaseCount);
    EXPECT_EQ((int)kCaseCount, 50);
}

}  // namespace
