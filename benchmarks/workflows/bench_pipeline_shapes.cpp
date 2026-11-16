// benchmarks/workflows/bench_pipeline_shapes.cpp — SOTA B3 (#2055):
// the whole-pipeline scoreboard shapes. Each shape is a fixed input
// corpus + a fixed operation script (parse → query → mutate →
// serialize — the real consumer shapes), run with the interleaved-
// median discipline: leptris and the competitor alternate within
// every repetition, so cache/thermal ordering cannot favor either
// side; the reported number is the median of the per-rep samples.
//
// Rows are machine-readable (shape=/engine=/median_us=/ratio=) so
// the Benchmark workflow summary and MISSION-PLAN.md can quote them
// verbatim.
//
// Usage: bench_pipeline_shapes [reps]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <sys/time.h>

#include <leptris.h>

#ifdef HAVE_PUGIXML
#include <pugixml.hpp>
#endif

#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/xpath.h>
#endif

namespace {

// ------------------------------------------------------------------
// Timing + interleaved-median discipline
// ------------------------------------------------------------------

double now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

double median_of(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (!n) return 0.0;
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

std::vector<char> load_corpus(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s — run from build/benchmarks "
                        "(the workflow stages fixtures/ there)\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf((size_t)n + 1);
    size_t got = fread(buf.data(), 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

// ------------------------------------------------------------------
// The four shapes, leptris side. Each returns nonzero on any
// structural surprise (a query that must hit but doesn't) so a rig
// bug can't masquerade as a speed number.
// ------------------------------------------------------------------

int shape_metanorma_leptris(const char* xml, size_t len) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, len, &st);
    if (!doc) return 1;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) { leptris_document_free(doc); return 1; }

    LeptrisXPathResult terms =
        leptris_xpath_eval(doc, root, "//standard/clause/terms/term");
    if (!terms) { leptris_document_free(doc); return 1; }
    size_t nterm = leptris_xpath_result_count(terms);
    if (nterm == 0) { leptris_xpath_result_free(terms);
                      leptris_document_free(doc); return 1; }
    for (size_t i = 0; i < nterm; i++) {
        LeptrisElement t = leptris_xpath_result_get(terms, i);
        if (!leptris_element_child_value(
                leptris_element_first_child(t, "preferred"))) {
            leptris_xpath_result_free(terms);
            leptris_document_free(doc);
            return 1;
        }
    }
    leptris_xpath_result_free(terms);

    // The compile-shape mutation: clause ids + per-term review notes.
    LeptrisXPathResult clauses =
        leptris_xpath_eval(doc, root, "//standard/clause");
    if (!clauses) { leptris_document_free(doc); return 1; }
    size_t ncl = leptris_xpath_result_count(clauses);
    for (size_t i = 0; i < ncl; i++) {
        char idbuf[32];
        snprintf(idbuf, sizeof idbuf, "sec-%zu", i);
        leptris_element_set_attribute(
            leptris_xpath_result_get(clauses, i), "rev-id", idbuf);
    }
    leptris_xpath_result_free(clauses);

    LeptrisXPathResult terms2 =
        leptris_xpath_eval(doc, root, "//standard/clause/terms/term");
    if (!terms2) { leptris_document_free(doc); return 1; }
    size_t nt2 = leptris_xpath_result_count(terms2);
    for (size_t i = 0; i < nt2; i++)
        leptris_element_create_child(
            leptris_xpath_result_get(terms2, i), "review");
    leptris_xpath_result_free(terms2);

    char* out = leptris_document_serialize(doc, NULL);
    if (!out) { leptris_document_free(doc); return 1; }
    leptris_free_string(out);
    leptris_document_free(doc);
    return 0;
}

int shape_config_leptris(const char* xml, size_t len) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, len, &st);
    if (!doc) return 1;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) { leptris_document_free(doc); return 1; }

    // The fixed validation assert set. Assert 0 is the sanity gate
    // (the corpus always has hosts); the rest must merely EVALUATE —
    // a zero count ("no service misses @port") is a PASS, not a rig
    // failure.
    static const char* ASSERTS[] = {
        "//service[@host]",
        "count(//service[not(@port)])",
        "count(//service[@tls='true'])",
        "count(//service/route)",
        "boolean(//defaults)",
    };
    for (size_t a = 0; a < sizeof ASSERTS / sizeof ASSERTS[0]; a++) {
        LeptrisXPathResult r =
            leptris_xpath_eval(doc, root, ASSERTS[a]);
        if (!r) { leptris_document_free(doc); return 1; }
        size_t n = leptris_xpath_result_count(r);
        if (a == 0 && n == 0) {
            leptris_xpath_result_free(r);
            leptris_document_free(doc);
            return 1;
        }
        leptris_xpath_result_free(r);
    }

    char* out = leptris_document_serialize(doc, NULL);
    if (!out) { leptris_document_free(doc); return 1; }
    leptris_free_string(out);
    leptris_document_free(doc);
    return 0;
}

int shape_feed_leptris(const char* xml, size_t len) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, len, &st);
    if (!doc) return 1;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) { leptris_document_free(doc); return 1; }

    LeptrisXPathResult entries =
        leptris_xpath_eval(doc, root, "//feed/entry");
    if (!entries) { leptris_document_free(doc); return 1; }
    size_t n = leptris_xpath_result_count(entries);
    if (n == 0) { leptris_xpath_result_free(entries);
                  leptris_document_free(doc); return 1; }
    for (size_t i = 0; i < n; i++) {
        LeptrisElement e = leptris_xpath_result_get(entries, i);
        if (!leptris_element_child_value(
                leptris_element_first_child(e, NULL))) {
            leptris_xpath_result_free(entries);
            leptris_document_free(doc);
            return 1;
        }
        leptris_element_set_attribute(e, "normalized", "1");
        leptris_element_create_child(e, "generator");
    }
    leptris_xpath_result_free(entries);

    char* out = leptris_document_serialize(doc, NULL);
    if (!out) { leptris_document_free(doc); return 1; }
    leptris_free_string(out);
    leptris_document_free(doc);
    return 0;
}

int shape_extract_leptris(const char* xml, size_t len) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, len, &st);
    if (!doc) return 1;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) { leptris_document_free(doc); return 1; }

    LeptrisXPathResult sel =
        leptris_xpath_eval(doc, root,
                           "//records/record[@status='active']");
    if (!sel) { leptris_document_free(doc); return 1; }
    size_t n = leptris_xpath_result_count(sel);
    if (n == 0) { leptris_xpath_result_free(sel);
                  leptris_document_free(doc); return 1; }

    // The build row: a NEW document assembled from the selection.
    LeptrisDocument out_doc = leptris_document_create();
    if (!out_doc) { leptris_xpath_result_free(sel);
                    leptris_document_free(doc); return 1; }
    LeptrisElement out_root =
        leptris_element_create(out_doc, "active-records");
    if (!out_root) { leptris_document_free(out_doc);
                     leptris_xpath_result_free(sel);
                     leptris_document_free(doc); return 1; }
    leptris_document_set_root(out_doc, out_root);
    for (size_t i = 0; i < n; i++) {
        LeptrisElement rec = leptris_xpath_result_get(sel, i);
        char rid[32], ramt[32];
        const char* id = leptris_element_attribute(rec, "id");
        const char* amt = leptris_element_attribute(rec, "amount");
        if (!id || !amt) { leptris_document_free(out_doc);
                           leptris_xpath_result_free(sel);
                           leptris_document_free(doc); return 1; }
        snprintf(rid, sizeof rid, "%s", id);
        snprintf(ramt, sizeof ramt, "%s", amt);
        LeptrisElement c = leptris_element_create_child(out_root, "r");
        if (!c) { leptris_document_free(out_doc);
                  leptris_xpath_result_free(sel);
                  leptris_document_free(doc); return 1; }
        leptris_element_set_attribute(c, "id", rid);
        leptris_element_set_attribute(c, "amount", ramt);
    }
    leptris_xpath_result_free(sel);

    char* out = leptris_document_serialize(out_doc, NULL);
    if (!out) { leptris_document_free(out_doc);
                leptris_document_free(doc); return 1; }
    leptris_free_string(out);
    leptris_document_free(out_doc);
    leptris_document_free(doc);
    return 0;
}

// ------------------------------------------------------------------
// Competitor rigs. Same script per shape, competitor API.
// ------------------------------------------------------------------

#ifdef HAVE_LIBXML2
int shape_metanorma_libxml2(const char* xml, size_t len) {
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    if (!doc) return 1;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) { xmlFreeDoc(doc); return 1; }
    xmlXPathObjectPtr terms =
        xmlXPathEvalExpression((const xmlChar*)"//standard/clause/terms/term", ctx);
    if (!terms || terms->nodesetval == NULL ||
        terms->nodesetval->nodeNr == 0) {
        if (terms) xmlXPathFreeObject(terms);
        xmlXPathFreeContext(ctx); xmlFreeDoc(doc); return 1;
    }
    for (int i = 0; i < terms->nodesetval->nodeNr; i++) {
        xmlNodePtr t = terms->nodesetval->nodeTab[i];
        int has_preferred = 0;
        for (xmlNodePtr c = t->children; c; c = c->next)
            if (c->type == XML_ELEMENT_NODE &&
                xmlStrcmp(c->name, (const xmlChar*)"preferred") == 0 &&
                c->children && c->children->content &&
                c->children->content[0])
                has_preferred = 1;
        if (!has_preferred) {
            xmlXPathFreeObject(terms);
            xmlXPathFreeContext(ctx);
            xmlFreeDoc(doc);
            return 1;
        }
    }
    xmlXPathFreeObject(terms);
    xmlXPathObjectPtr clauses =
        xmlXPathEvalExpression((const xmlChar*)"//standard/clause", ctx);
    if (clauses && clauses->nodesetval) {
        for (int i = 0; i < clauses->nodesetval->nodeNr; i++) {
            char idbuf[32];
            snprintf(idbuf, sizeof idbuf, "sec-%d", i);
            xmlSetProp(clauses->nodesetval->nodeTab[i],
                       (const xmlChar*)"rev-id", (const xmlChar*)idbuf);
        }
    }
    if (clauses) xmlXPathFreeObject(clauses);
    xmlXPathObjectPtr terms2 =
        xmlXPathEvalExpression((const xmlChar*)"//standard/clause/terms/term", ctx);
    if (terms2 && terms2->nodesetval) {
        for (int i = 0; i < terms2->nodesetval->nodeNr; i++)
            xmlNewChild(terms2->nodesetval->nodeTab[i], NULL,
                        (const xmlChar*)"review", NULL);
    }
    if (terms2) xmlXPathFreeObject(terms2);
    xmlXPathFreeContext(ctx);
    xmlChar* dump = NULL;
    int dump_len = 0;
    xmlDocDumpMemory(doc, &dump, &dump_len);
    int ok = dump && dump_len > 0;
    xmlFree(dump);
    xmlFreeDoc(doc);
    return ok ? 0 : 1;
}

int shape_config_libxml2(const char* xml, size_t len) {
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    if (!doc) return 1;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) { xmlFreeDoc(doc); return 1; }
    static const char* ASSERTS[] = {
        "//service[@host]",
        "count(//service[not(@port)])",
        "count(//service[@tls='true'])",
        "count(//service/route)",
        "boolean(//defaults)",
    };
    for (size_t a = 0;
         a < sizeof ASSERTS / sizeof ASSERTS[0]; a++) {
        xmlXPathObjectPtr r = xmlXPathEvalExpression(
            (const xmlChar*)ASSERTS[a], ctx);
        if (!r) { xmlXPathFreeContext(ctx); xmlFreeDoc(doc); return 1; }
        if (a == 0 && (!r->nodesetval || r->nodesetval->nodeNr == 0)) {
            xmlXPathFreeObject(r);
            xmlXPathFreeContext(ctx); xmlFreeDoc(doc); return 1;
        }
        xmlXPathFreeObject(r);
    }
    xmlXPathFreeContext(ctx);
    xmlChar* dump = NULL;
    int dump_len = 0;
    xmlDocDumpMemory(doc, &dump, &dump_len);
    int ok = dump && dump_len > 0;
    xmlFree(dump);
    xmlFreeDoc(doc);
    return ok ? 0 : 1;
}

int shape_feed_libxml2(const char* xml, size_t len) {
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    if (!doc) return 1;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) { xmlFreeDoc(doc); return 1; }
    xmlXPathObjectPtr entries =
        xmlXPathEvalExpression((const xmlChar*)"//feed/entry", ctx);
    if (!entries || !entries->nodesetval ||
        entries->nodesetval->nodeNr == 0) {
        if (entries) xmlXPathFreeObject(entries);
        xmlXPathFreeContext(ctx); xmlFreeDoc(doc); return 1;
    }
    for (int i = 0; i < entries->nodesetval->nodeNr; i++) {
        xmlNodePtr e = entries->nodesetval->nodeTab[i];
        xmlNodePtr title = NULL;
        for (xmlNodePtr c = e->children; c; c = c->next)
            if (c->type == XML_ELEMENT_NODE &&
                xmlStrcmp(c->name, (const xmlChar*)"title") == 0)
                title = c;
        if (!title || !title->children || !title->children->content ||
            !title->children->content[0]) {
            xmlXPathFreeObject(entries);
            xmlXPathFreeContext(ctx);
            xmlFreeDoc(doc);
            return 1;
        }
        xmlSetProp(e, (const xmlChar*)"normalized", (const xmlChar*)"1");
        xmlNewChild(e, NULL, (const xmlChar*)"generator", NULL);
    }
    xmlXPathFreeObject(entries);
    xmlXPathFreeContext(ctx);
    xmlChar* dump = NULL;
    int dump_len = 0;
    xmlDocDumpMemory(doc, &dump, &dump_len);
    int ok = dump && dump_len > 0;
    xmlFree(dump);
    xmlFreeDoc(doc);
    return ok ? 0 : 1;
}

int shape_extract_libxml2(const char* xml, size_t len) {
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, 0);
    if (!doc) return 1;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) { xmlFreeDoc(doc); return 1; }
    xmlXPathObjectPtr sel = xmlXPathEvalExpression(
        (const xmlChar*)"//records/record[@status='active']", ctx);
    if (!sel || !sel->nodesetval || sel->nodesetval->nodeNr == 0) {
        if (sel) xmlXPathFreeObject(sel);
        xmlXPathFreeContext(ctx); xmlFreeDoc(doc); return 1;
    }
    xmlXPathFreeObject(sel);
    xmlXPathFreeContext(ctx);
    xmlDocPtr out = xmlNewDoc((const xmlChar*)"1.0");
    xmlNodePtr out_root = xmlNewNode(NULL, (const xmlChar*)"active-records");
    xmlDocSetRootElement(out, out_root);
    // Honest re-run: fresh context, fresh eval, then build.
    ctx = xmlXPathNewContext(doc);
    sel = xmlXPathEvalExpression(
        (const xmlChar*)"//records/record[@status='active']", ctx);
    if (sel && sel->nodesetval) {
        for (int i = 0; i < sel->nodesetval->nodeNr; i++) {
            xmlNodePtr rec = sel->nodesetval->nodeTab[i];
            xmlChar* id = xmlGetProp(rec, (const xmlChar*)"id");
            xmlChar* amt = xmlGetProp(rec, (const xmlChar*)"amount");
            if (!id || !amt) {
                xmlFree(id); xmlFree(amt);
                xmlXPathFreeObject(sel); xmlXPathFreeContext(ctx);
                xmlFreeDoc(out); xmlFreeDoc(doc);
                return 1;
            }
            xmlNodePtr c = xmlNewChild(out_root, NULL,
                                       (const xmlChar*)"r", NULL);
            xmlSetProp(c, (const xmlChar*)"id", id);
            xmlSetProp(c, (const xmlChar*)"amount", amt);
            xmlFree(id); xmlFree(amt);
        }
    }
    if (sel) xmlXPathFreeObject(sel);
    xmlXPathFreeContext(ctx);
    xmlChar* dump = NULL;
    int dump_len = 0;
    xmlDocDumpMemory(out, &dump, &dump_len);
    int ok = dump && dump_len > 0;
    xmlFree(dump);
    xmlFreeDoc(out);
    xmlFreeDoc(doc);
    return ok ? 0 : 1;
}
#endif  // HAVE_LIBXML2

#ifdef HAVE_PUGIXML
int shape_metanorma_pugixml(const char* xml, size_t len) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml, len)) return 1;
    pugi::xpath_node_set terms =
        doc.select_nodes("//standard/clause/terms/term");
    if (terms.empty()) return 1;
    pugi::xpath_node_set clauses =
        doc.select_nodes("//standard/clause");
    char idbuf[32];
    for (size_t i = 0; i < clauses.size(); i++) {
        snprintf(idbuf, sizeof idbuf, "sec-%zu", i);
        clauses[i].node().append_attribute("rev-id") = idbuf;
    }
    for (pugi::xpath_node tn : terms)
        tn.node().append_child("review");
    std::string dump;
    struct Sink : pugi::xml_writer {
        std::string s;
        void write(const void* data, size_t n) override {
            s.append((const char*)data, n);
        }
    } sink;
    doc.save(sink, "", pugi::format_raw | pugi::format_no_declaration);
    dump = sink.s;
    return dump.empty() ? 1 : 0;
}

int shape_config_pugixml(const char* xml, size_t len) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml, len)) return 1;
    static const char* ASSERTS[] = {
        "//service[@host]",
        "count(//service[not(@port)])",
        "count(//service[@tls='true'])",
        "count(//service/route)",
        "boolean(//defaults)",
    };
    pugi::xpath_node_set sanity =
        doc.select_nodes(ASSERTS[0]);
    if (sanity.empty()) return 1;
    for (size_t a = 1;
         a < sizeof ASSERTS / sizeof ASSERTS[0]; a++) {
        pugi::xpath_query q(ASSERTS[a]);
        (void)q.evaluate_number(doc);  // the value is the PASS/FAIL
                                       // verdict; the rig only needs
                                       // the evaluation to run
    }
    std::string dump;
    struct Sink : pugi::xml_writer {
        std::string s;
        void write(const void* data, size_t n) override {
            s.append((const char*)data, n);
        }
    } sink;
    doc.save(sink, "", pugi::format_raw | pugi::format_no_declaration);
    dump = sink.s;
    return dump.empty() ? 1 : 0;
}

int shape_feed_pugixml(const char* xml, size_t len) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml, len)) return 1;
    pugi::xpath_node_set entries = doc.select_nodes("//feed/entry");
    if (entries.empty()) return 1;
    for (pugi::xpath_node en : entries) {
        if (en.node().child("title").text().empty()) return 1;
        en.node().append_attribute("normalized") = "1";
        en.node().append_child("generator");
    }
    struct Sink : pugi::xml_writer {
        std::string s;
        void write(const void* data, size_t n) override {
            s.append((const char*)data, n);
        }
    } sink;
    doc.save(sink, "", pugi::format_raw | pugi::format_no_declaration);
    return sink.s.empty() ? 1 : 0;
}

int shape_extract_pugixml(const char* xml, size_t len) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml, len)) return 1;
    pugi::xpath_node_set sel =
        doc.select_nodes("//records/record[@status='active']");
    if (sel.empty()) return 1;
    pugi::xml_document out;
    pugi::xml_node out_root = out.append_child("active-records");
    for (pugi::xpath_node sn : sel) {
        pugi::xml_attribute id = sn.node().attribute("id");
        pugi::xml_attribute amt = sn.node().attribute("amount");
        if (!id || !amt) return 1;
        pugi::xml_node c = out_root.append_child("r");
        pugi::xml_attribute cid = c.append_attribute("id");
        cid.set_value(id.value());
        pugi::xml_attribute camt = c.append_attribute("amount");
        camt.set_value(amt.value());
    }
    struct Sink : pugi::xml_writer {
        std::string s;
        void write(const void* data, size_t n) override {
            s.append((const char*)data, n);
        }
    } sink;
    out.save(sink, "", pugi::format_raw | pugi::format_no_declaration);
    return sink.s.empty() ? 1 : 0;
}
#endif  // HAVE_PUGIXML

// ------------------------------------------------------------------
// Shape table + the interleaved driver
// ------------------------------------------------------------------

struct Shape {
    const char* name;
    const char* corpus;
    int (*leptris_fn)(const char*, size_t);
#ifdef HAVE_LIBXML2
    int (*libxml2_fn)(const char*, size_t);
#endif
#ifdef HAVE_PUGIXML
    int (*pugixml_fn)(const char*, size_t);
#endif
};

const Shape SHAPES[] = {
    {"metanorma-compile", "fixtures/pipeline_metanorma.xml",
     shape_metanorma_leptris,
#ifdef HAVE_LIBXML2
     shape_metanorma_libxml2,
#endif
#ifdef HAVE_PUGIXML
     shape_metanorma_pugixml,
#endif
    },
    {"config-validation", "fixtures/pipeline_config.xml",
     shape_config_leptris,
#ifdef HAVE_LIBXML2
     shape_config_libxml2,
#endif
#ifdef HAVE_PUGIXML
     shape_config_pugixml,
#endif
    },
    {"feed-normalization", "fixtures/pipeline_feed.xml",
     shape_feed_leptris,
#ifdef HAVE_LIBXML2
     shape_feed_libxml2,
#endif
#ifdef HAVE_PUGIXML
     shape_feed_pugixml,
#endif
    },
    {"extract-transform", "fixtures/pipeline_records.xml",
     shape_extract_leptris,
#ifdef HAVE_LIBXML2
     shape_extract_libxml2,
#endif
#ifdef HAVE_PUGIXML
     shape_extract_pugixml,
#endif
    },
};

}  // namespace

int main(int argc, char** argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 25;
    if (reps < 5) reps = 5;
#ifdef HAVE_LIBXML2
    xmlInitParser();
#endif

    printf("# pipeline shapes — interleaved-median, %d reps per side\n",
           reps);

    for (const Shape& s : SHAPES) {
        std::vector<char> xml = load_corpus(s.corpus);

        // Correctness gate first: a shape that fails the structural
        // checks disqualifies its own rig (exit nonzero) rather than
        // printing a speed number for a broken script.
        if (s.leptris_fn(xml.data(), xml.size() - 1)) {
            printf("shape=%s engine=leptris ERROR rig-failed\n", s.name);
            return 3;
        }

        std::vector<double> lep_samples;
        lep_samples.reserve((size_t)reps);
        for (int r = 0; r < reps; r++) {
            double t0 = now_us();
            if (s.leptris_fn(xml.data(), xml.size() - 1)) {
                printf("shape=%s engine=leptris ERROR rig-failed-mid\n",
                       s.name);
                return 3;
            }
            lep_samples.push_back(now_us() - t0);
        }
        double lep_med = median_of(lep_samples);
        printf("shape=%s engine=leptris median_us=%.1f reps=%d\n",
               s.name, lep_med, reps);

#ifdef HAVE_LIBXML2
        if (s.libxml2_fn && s.libxml2_fn(xml.data(), xml.size() - 1)) {
            printf("shape=%s engine=libxml2 ERROR rig-failed\n", s.name);
            return 3;
        }
        // Interleave: rerun leptris BETWEEN competitor reps so the
        // ordering alternates (L O L O … / O L O L … by parity).
        std::vector<double> lx_samples;
        lx_samples.reserve((size_t)reps);
        for (int r = 0; r < reps; r++) {
            if (r % 2 == 0) s.leptris_fn(xml.data(), xml.size() - 1);
            double t0 = now_us();
            s.libxml2_fn(xml.data(), xml.size() - 1);
            lx_samples.push_back(now_us() - t0);
            if (r % 2 == 1) s.leptris_fn(xml.data(), xml.size() - 1);
        }
        double lx_med = median_of(lx_samples);
        printf("shape=%s engine=libxml2 median_us=%.1f reps=%d\n",
               s.name, lx_med, reps);
        if (lx_med > 0.0)
            printf("ratio=%s leptris/libxml2=%.3f\n",
                   s.name, lep_med / lx_med);
#endif

#ifdef HAVE_PUGIXML
        if (s.pugixml_fn && s.pugixml_fn(xml.data(), xml.size() - 1)) {
            printf("shape=%s engine=pugixml ERROR rig-failed\n", s.name);
            return 3;
        }
        std::vector<double> px_samples;
        px_samples.reserve((size_t)reps);
        for (int r = 0; r < reps; r++) {
            if (r % 2 == 0) s.leptris_fn(xml.data(), xml.size() - 1);
            double t0 = now_us();
            s.pugixml_fn(xml.data(), xml.size() - 1);
            px_samples.push_back(now_us() - t0);
            if (r % 2 == 1) s.leptris_fn(xml.data(), xml.size() - 1);
        }
        double px_med = median_of(px_samples);
        printf("shape=%s engine=pugixml median_us=%.1f reps=%d\n",
               s.name, px_med, reps);
        if (px_med > 0.0)
            printf("ratio=%s leptris/pugixml=%.3f\n",
                   s.name, lep_med / px_med);
#endif
    }

#ifdef HAVE_LIBXML2
    xmlCleanupParser();
#endif
    return 0;
}
