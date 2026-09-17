// test/rng/test_rng.cpp — RELAX NG phase-1 specs (#878): the
// XML-syntax schema lowers into the pattern IR with the right
// shapes; errors reject bad schemas.

#include <gtest/gtest.h>

#include "leptris.h"
#include "leptris/error.h"
#include "rng_internal.h"

#include <cstring>

namespace {

#define RNGNS "http://relaxng.org/ns/structure/1.0"

#define SCHEMA(body) \
    ("<grammar xmlns='" RNGNS "'>" body "</grammar>")

size_t count_kind(const RngPattern* p, RngPatternKind k) {
    if (!p) return 0;
    return (p->kind == k ? 1u : 0u) + count_kind(p->first_child, k) +
           count_kind(p->next, k);
}

size_t total_patterns(const RngPattern* p) {
    if (!p) return 0;
    return 1 + total_patterns(p->first_child) + total_patterns(p->next);
}

TEST(RngParse, LowersASimpleSchema) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><element name='book'>"
        "<attribute name='id'/>"
        "<oneOrMore><element name='author'><text/></element></oneOrMore>"
        "</element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);

    const RngGrammar* g = ((struct leptris_relaxng*)rng)->grammar;
    ASSERT_NE(g->start, nullptr);
    EXPECT_EQ(count_kind(g->start, RNG_ELEMENT), 2u);
    EXPECT_EQ(count_kind(g->start, RNG_ATTRIBUTE), 1u);
    EXPECT_EQ(count_kind(g->start, RNG_ONE_OR_MORE), 1u);
    EXPECT_EQ(count_kind(g->start, RNG_TEXT), 1u);
    /* start wrapper (GROUP) + 2 element + attr + repeat + text */
    EXPECT_EQ(total_patterns(g->start), 6u);

    /* The root element pattern carries its name. */
    const RngPattern* book = g->start->first_child;
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->kind, RNG_ELEMENT);
    EXPECT_STREQ(book->name, "book");

    leptris_rng_free(rng);
}

TEST(RngParse, RefsAndDefines) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><ref name='content'/></start>"
        "<define name='content'>"
        "<element name='e'><text/></element>"
        "</define>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);

    const RngGrammar* g = ((struct leptris_relaxng*)rng)->grammar;
    EXPECT_EQ(count_kind(g->start, RNG_REF), 1u);
    const RngPattern* ref = g->start->first_child;
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->kind, RNG_REF);
    EXPECT_STREQ(ref->name, "content");

    const RngDefine* d = g->defines;
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "content");
    ASSERT_NE(d->body, nullptr);
    EXPECT_EQ(d->body->kind, RNG_ELEMENT);

    leptris_rng_free(rng);
}

TEST(RngParse, CombineMergesDefines) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><ref name='c'/></start>"
        "<define name='c'><element name='x'><empty/></element></define>"
        "<define name='c' combine='choice'>"
        "<element name='y'><empty/></element></define>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);

    const RngDefine* d = ((struct leptris_relaxng*)rng)->grammar->defines;
    ASSERT_NE(d, nullptr);
    ASSERT_NE(d->body, nullptr);
    EXPECT_EQ(d->body->kind, RNG_CHOICE);
    EXPECT_EQ(count_kind(d->body, RNG_ELEMENT), 2u);

    leptris_rng_free(rng);
}

TEST(RngParse, DataValueParamExceptListMixed) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><element name='r'>"
        "<data type='integer'><param name='minInclusive'>0</param>"
        "<except><value>5</value></except></data>"
        "<list><data type='token'/></list>"
        "<mixed><element name='m'><empty/></element></mixed>"
        "<value ns='urn:x'>vx</value>"
        "</element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);

    const RngPattern* root =
        ((struct leptris_relaxng*)rng)->grammar->start->first_child;
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(count_kind(root, RNG_DATA), 2u);
    EXPECT_EQ(count_kind(root, RNG_PARAM), 1u);
    EXPECT_EQ(count_kind(root, RNG_EXCEPT), 1u);
    EXPECT_EQ(count_kind(root, RNG_LIST), 1u);
    EXPECT_EQ(count_kind(root, RNG_MIXED), 1u);
    /* Two VALUE nodes: <except><value>5</value></except> and the
     * direct <value ns='urn:x'>vx</value>. */
    EXPECT_EQ(count_kind(root, RNG_VALUE), 2u);

    /* Find the direct value node and check its content + ns. */
    const RngPattern* v = root->first_child;
    while (v && v->kind != RNG_VALUE) {
        v = v->next;
    }
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->value, "vx");
    EXPECT_STREQ(v->ns, "urn:x");

    leptris_rng_free(rng);
}

TEST(RngParse, RejectsWrongRootNamespace) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] =
        "<grammar xmlns='http://example.com/wrong'><start/></grammar>";
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(rng, nullptr);
}

TEST(RngParse, RejectsUnknownPatternElement) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><element name='e'><bogus/></element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(rng, nullptr);
}

TEST(RngParse, SkipsForeignNamespaceAnnotations) {
    /* RELAX NG §"grammar": elements in a foreign namespace — the
     * DTD-compatibility annotations (a:documentation et al.), or any
     * other foreign element — are ignored. Production schemas
     * (metanorma's isodoc-compile.rng) are saturated with them. */
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] =
        "<grammar xmlns='" RNGNS "'"
        " xmlns:a='http://relaxng.org/ns/compatibility/annotations/1.0'"
        " datatypeLibrary='http://www.w3.org/2001/XMLSchema-datatypes'>"
        "<start><element name='e'>"
        "<a:documentation>docs</a:documentation>"
        "<attribute name='id'>"
        "<a:documentation>the id</a:documentation>"
        "<data type='token'/>"
        "</attribute>"
        "</element></start></grammar>";
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK) << "annotations must be skipped, not rejected";
    ASSERT_NE(rng, nullptr);
    const RngGrammar* g = ((struct leptris_relaxng*)rng)->grammar;
    EXPECT_EQ(count_kind(g->start, RNG_ATTRIBUTE), 1u);
    EXPECT_EQ(count_kind(g->start, RNG_DATA), 1u);
    leptris_rng_free(rng);
}

TEST(RngParse, AnyNameNameClassMatchesAnyElement) {
    /* <element><anyName/> is the wildcard name class (RELAX NG
     * 4.14): biblio.rng's recursive AnyElement define. */
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><element name='r'>"
        "<oneOrMore><element><anyName/>"
        "<attribute><anyName/></attribute><text/>"
        "</element></oneOrMore>"
        "</element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK) << "anyName must parse";
    ASSERT_NE(rng, nullptr);
    const char doc[] = "<r xmlns='urn:t'><x a='1'>t</x><y>b='2'>u</y></r>";
    LeptrisDocument d = leptris_parse_string(doc, sizeof(doc) - 1, NULL);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d), 1);
    leptris_document_free(d);
    leptris_rng_free(rng);
}

TEST(RngParse, CombineSurvivesAcrossMerges) {
    /* @combine may sit on the FIRST declaration: later same-name
     * defines merge without repeating it. The merge path used to
     * free d->combine and re-store the dangling pointer (ASAN:
     * use-after-free on the metanorma include chain). */
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><ref name='c'/></start>"
        "<define name='c' combine='choice'>"
        "<element name='a'><empty/></element></define>"
        "<define name='c'><element name='b'><empty/></element></define>"
        "<define name='c'><element name='d'><empty/></element></define>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);
    const char doc[] = "<d/>";
    LeptrisDocument d2 = leptris_parse_string(doc, sizeof(doc) - 1, NULL);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d2), 1);
    leptris_document_free(d2);
    leptris_rng_free(rng);
}

TEST(RngParse, RejectsUncombinedRedefinition) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><ref name='c'/></start>"
        "<define name='c'><empty/></define>"
        "<define name='c'><empty/></define>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(rng, nullptr);
}

TEST(RngParse, BareElementRoot) {
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] =
        "<element name='r' xmlns='" RNGNS "'><text/></element>";
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);
    const RngPattern* start =
        ((struct leptris_relaxng*)rng)->grammar->start;
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(start->kind, RNG_ELEMENT);
    EXPECT_STREQ(start->name, "r");
    leptris_rng_free(rng);
}


// ---- #878: accumulate multiple errors + expose per-error fields ----

TEST(RngErrors, AccumulatesAndExposesPerError) {
    /* schema declares RELAX NG namespace via xmlns. */
    const char* sch =
        "<element xmlns='http://relaxng.org/ns/structure/1.0' name='r'>"
        "<choice><element name='a'/>"
        "<element name='b'/></choice></element>";
    LeptrisStatus st = LEPTRIS_OK; LeptrisRelaxNG schema = leptris_rng_parse(
        sch, std::strlen(sch), &st);
    ASSERT_NE(schema, nullptr);
    const char* ins = "<r xmlns='urn:t'><x/><y/></r>";
    LeptrisDocument doc = leptris_parse_string(
        ins, std::strlen(ins), NULL);
    int valid = leptris_rng_validate(schema, doc);
    EXPECT_EQ(valid, 0);
    /* Two wrong children + the parent-incomplete followup — Jing
     * reports all three; the engine used to stop at the first. */
    EXPECT_EQ(leptris_rng_error_count(schema), 3u);
    /* Each error carries a non-zero line (the offending element's
     * source line) and a Jing-vocabulary message. */
    for (unsigned i = 0; i < 3; i++) {
        EXPECT_GT(leptris_rng_error_line(schema, i), 0u) << i;
        EXPECT_NE(leptris_rng_error_message(schema, i), nullptr) << i;
    }
    EXPECT_NE(strstr(leptris_rng_error_message(schema, 0),
                     "not allowed anywhere"), nullptr);
    EXPECT_NE(strstr(leptris_rng_error_message(schema, 2),
                     "incomplete"), nullptr);
    /* First-error accessor remains backward-compatible. */
    EXPECT_NE(leptris_rng_error(schema), nullptr);
    /* Out-of-range index returns NULL and the count is stable. */
    EXPECT_EQ(leptris_rng_error_message(schema, 99), nullptr);
    leptris_document_free(doc);
    leptris_rng_free(schema);
}

// ---- #1137: optional omitted must stay valid ----------------------

TEST(RngRegression, OmittedOptionalElementStaysValid) {
    /* v1.9.179 regression: the speculative probe of an omitted
     * <optional> child poisoned the matcher's failed short-circuit,
     * turning valid documents invalid. */
    const char* sch =
        "<element name='library' xmlns='" RNGNS "'>"
        "<oneOrMore><element name='book'>"
        "<attribute name='id'><text/></attribute>"
        "<optional><element name='title'><text/></element></optional>"
        "<element name='author'><text/></element>"
        "</element></oneOrMore></element>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
    ASSERT_NE(rng, nullptr);
    const char* ins =
        "<library><book id='1'><author>A</author></book></library>";
    LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d), 1)
        << "omitted optional must validate (Jing: valid)";
    /* The title PRESENT form stays valid too. */
    const char* ins2 =
        "<library><book id='1'><title>T</title><author>A</author>"
        "</book></library>";
    LeptrisDocument d2 = leptris_parse_string(ins2, strlen(ins2), NULL);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d2), 1);
    leptris_document_free(d);
    leptris_document_free(d2);
    leptris_rng_free(rng);
}

// ---- #1126: structured kinds on the diag records ------------------

TEST(RngDiagKinds, CorpusCasesCarryTheirKinds) {
    /* 003: <r><c/></r> vs choice(a|b) — child error then parent
     * incomplete. */
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='r'>"
            "<choice><element name='a'/><element name='b'/></choice>"
            "</element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<r><c/></r>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 2);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE);
        EXPECT_EQ(r->diags[1].kind, LEPTRIS_DIAG_INCOMPLETE);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
    /* 006: group(a,b) vs <b/><a/> — not-allowed-yet + overflow. */
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='r'>"
            "<group><element name='a'/><element name='b'/></group>"
            "</element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<r><b/><a/></r>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 2);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_NOT_ALLOWED_YET);
        EXPECT_EQ(r->diags[1].kind, LEPTRIS_DIAG_NOT_ALLOWED_HERE);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
    /* 009 / 017 / 000 / 001: character content, attr value, missing
     * attr, extra attr. */
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='n'>"
            "<data type='integer'/></element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<n>x</n>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 1);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_CHAR_CONTENT_INVALID);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='v'><value>yes</value></element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<v>no</v>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 1);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_CHAR_CONTENT_INVALID);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='a'><attribute name='x'/></element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<a/>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 1);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_MISSING_REQUIRED_ATTR);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
    {
        const char* sch =
            "<element xmlns='" RNGNS "' name='a'><empty/></element>";
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
        ASSERT_NE(rng, nullptr);
        const char* ins = "<a x='1'/>";
        LeptrisDocument d = leptris_parse_string(ins, strlen(ins), NULL);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(leptris_rng_validate(rng, d), 0);
        struct leptris_relaxng* r = (struct leptris_relaxng*)rng;
        ASSERT_EQ(r->diag_count, 1);
        EXPECT_EQ(r->diags[0].kind, LEPTRIS_DIAG_ATTR_NOT_ALLOWED);
        leptris_document_free(d);
        leptris_rng_free(rng);
    }
}
}  // namespace

/* ---- phase 2: the core validator ------------------------------ */

static int validates(const char* schema_body, const char* doc) {
    LeptrisStatus st = LEPTRIS_OK;
    char sch[2048];
    snprintf(sch, sizeof(sch), "<grammar xmlns='%s'>%s</grammar>",
             RNGNS, schema_body);
    LeptrisRelaxNG rng = leptris_rng_parse(sch, strlen(sch), &st);
    if (!rng) return -1;
    LeptrisDocument d = leptris_parse_string(doc, strlen(doc), &st);
    if (!d) { leptris_rng_free(rng); return -2; }
    int ok = leptris_rng_validate(rng, d);
    leptris_document_free(d);
    leptris_rng_free(rng);
    return ok;
}

TEST(RngValidate, AcceptsAMatchingDocument) {
    EXPECT_EQ(validates(
        "<start><element name='book'>"
        "<attribute name='id'/>"
        "<oneOrMore><element name='author'><text/></element></oneOrMore>"
        "</element></start>",
        "<book id='b1'><author>A</author><author>B</author></book>"), 1);
}

TEST(RngValidate, RejectsWrongRootName) {
    EXPECT_EQ(validates(
        "<start><element name='book'><empty/></element></start>",
        "<chapter/>"), 0);
}

TEST(RngValidate, RejectsMissingRequiredAttribute) {
    EXPECT_EQ(validates(
        "<start><element name='e'><attribute name='id'/></element></start>",
        "<e/>"), 0);
}

TEST(RngValidate, RejectsUndeclaredAttribute) {
    EXPECT_EQ(validates(
        "<start><element name='e'><empty/></element></start>",
        "<e id='x'/>"), 0);
}

TEST(RngValidate, ChoiceAcceptsEitherAlternative) {
    const char* sch =
        "<start><element name='r'><choice>"
        "<element name='a'><empty/></element>"
        "<element name='b'><empty/></element>"
        "</choice></element></start>";
    EXPECT_EQ(validates(sch, "<r><a/></r>"), 1);
    EXPECT_EQ(validates(sch, "<r><b/></r>"), 1);
    EXPECT_EQ(validates(sch, "<r><c/></r>"), 0);
}

TEST(RngValidate, RepeatsEnforceCardinality) {
    const char* one =
        "<start><element name='r'><oneOrMore>"
        "<element name='i'><empty/></element>"
        "</oneOrMore></element></start>";
    EXPECT_EQ(validates(one, "<r><i/></r>"), 1);
    EXPECT_EQ(validates(one, "<r/>"), 0);
    const char* zero =
        "<start><element name='r'><zeroOrMore>"
        "<element name='i'><empty/></element>"
        "</zeroOrMore></element></start>";
    EXPECT_EQ(validates(zero, "<r/>"), 1);
}

TEST(RngValidate, DataAndValueLeaves) {
    EXPECT_EQ(validates(
        "<start><element name='n'><data type='integer'/></element></start>",
        "<n>42</n>"), 1);
    EXPECT_EQ(validates(
        "<start><element name='n'><data type='integer'/></element></start>",
        "<n>x</n>"), 0);
    EXPECT_EQ(validates(
        "<start><element name='n'><value>yes</value></element></start>",
        "<n>yes</n>"), 1);
    EXPECT_EQ(validates(
        "<start><element name='n'><value>yes</value></element></start>",
        "<n>no</n>"), 0);
}

TEST(RngValidate, TextAndDataStringAcceptEmptyContent) {
    /* RELAX NG: <text/> and <data type='string'/> match zero-length
     * and whitespace-only content; integer does not (Jing-confirmed). */
    const char* text_sch =
        "<start><element name='t'><text/></element></start>";
    EXPECT_EQ(validates(text_sch, "<t/>"), 1);
    EXPECT_EQ(validates(text_sch, "<t>  </t>"), 1);
    EXPECT_EQ(validates(text_sch, "<t><x/></t>"), 0);
    const char* str_sch =
        "<start><element name='n'><data type='string'/></element></start>";
    EXPECT_EQ(validates(str_sch, "<n/>"), 1);
    EXPECT_EQ(validates(str_sch, "<n>  </n>"), 1);
    const char* int_sch =
        "<start><element name='n'><data type='integer'/></element></start>";
    EXPECT_EQ(validates(int_sch, "<n/>"), 0);
    EXPECT_EQ(validates(int_sch, "<n>  </n>"), 0);
    EXPECT_EQ(validates(int_sch, "<n>5</n>"), 1);
}

TEST(RngValidate, ErrorCarriesJingShape) {
    LeptrisStatus st = LEPTRIS_OK;
    const char sch[] =
        "<grammar xmlns='" RNGNS "'>"
        "<start><element name='e'><attribute name='id'/></element></start>"
        "</grammar>";
    LeptrisRelaxNG rng = leptris_rng_parse(sch, sizeof(sch) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    const char doc[] = "<e/>\n";
    LeptrisDocument d = leptris_parse_string(doc, sizeof(doc) - 1, &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d), 0);
    const char* err = leptris_rng_error(rng);
    ASSERT_NE(err, nullptr);
    /* Jing shape: "line:column: error: message" — the parser-recorded
     * start-tag column for <e/> is 5 (the byte after '>'). */
    EXPECT_NE(strstr(err, "1:5: error:"), nullptr) << err;
    EXPECT_NE(strstr(err, "attribute"), nullptr) << err;
    leptris_document_free(d);
    leptris_rng_free(rng);
}

/* ---- phase 3: list tokens, mixed text, ref-cycle safety ------- */

TEST(RngValidate, ListTokenizesWhitespaceSeparatedTokens) {
    const char* sch =
        "<start><element name='r'><list><oneOrMore>"
        "<data type='integer'/>"
        "</oneOrMore></list></element></start>";
    EXPECT_EQ(validates(sch, "<r>1 2 3</r>"), 1);
    EXPECT_EQ(validates(sch, "<r>1 x 3</r>"), 0);
    EXPECT_EQ(validates(sch, "<r>  </r>"), 0);
}

TEST(RngValidate, ListValueTokens) {
    EXPECT_EQ(validates(
        "<start><element name='r'><list><value>a</value><value>b</value>"
        "</list></element></start>",
        "<r>a b</r>"), 1);
    EXPECT_EQ(validates(
        "<start><element name='r'><list><value>a</value><value>b</value>"
        "</list></element></start>",
        "<r>b a</r>"), 0);
}

TEST(RngValidate, MixedAllowsTextAroundElements) {
    EXPECT_EQ(validates(
        "<start><element name='p'><mixed>"
        "<zeroOrMore><element name='em'><text/></element></zeroOrMore>"
        "</mixed></element></start>",
        "<p>hello <em>x</em> world</p>"), 1);
}

TEST(RngValidate, RefRecursionIsGuarded) {
    /* A cyclic define (item -> item*) over a non-recursive document:
     * must terminate with a verdict, not hang. */
    EXPECT_EQ(validates(
        "<start><ref name='i'/></start>"
        "<define name='i'>"
        "<element name='item'>"
        "<choice><empty/><ref name='i'/></choice>"
        "</element></define>",
        "<item><item><item/></item></item>"), 1);
}

/* ---- phase 4: Jing-corpus conformance fixes --------------------- */

TEST(RngValidate, InterleaveConsumesEachChildOnceAndRequiresAll) {
    const char* sch =
        "<start><element name='r'><interleave>"
        "<element name='a'><empty/></element>"
        "<element name='b'><empty/></element>"
        "</interleave></element></start>";
    EXPECT_EQ(validates(sch, "<r><a/><b/></r>"), 1);
    EXPECT_EQ(validates(sch, "<r><b/><a/></r>"), 1);
    EXPECT_EQ(validates(sch, "<r><a/><a/></r>"), 0);
    EXPECT_EQ(validates(sch, "<r><a/></r>"), 0);
}

TEST(RngValidate, ListRejectsTokensNoLeafAccepts) {
    const char* sch =
        "<start><element name='l'><list><oneOrMore>"
        "<data type='integer'/>"
        "</oneOrMore></list></element></start>";
    EXPECT_EQ(validates(sch, "<l>1 2 3</l>"), 1);
    EXPECT_EQ(validates(sch, "<l>1 x</l>"), 0);
    EXPECT_EQ(validates(sch, "<l>x 1</l>"), 0);
}

TEST(RngValidate, StartChoiceFromCombinedDefine) {
    const char* sch =
        "<start><ref name='c'/></start>"
        "<define name='c'><element name='x'><empty/></element></define>"
        "<define name='c' combine='choice'>"
        "<element name='y'><empty/></element></define>";
    EXPECT_EQ(validates(sch, "<x/>"), 1);
    EXPECT_EQ(validates(sch, "<y/>"), 1);
    EXPECT_EQ(validates(sch, "<z/>"), 0);
}

TEST(RngValidate, InvalidVerdictAlwaysCarriesAnError) {
    LeptrisStatus st = LEPTRIS_OK;
    const char sch[] =
        "<grammar xmlns='" RNGNS "'>"
        "<start><ref name='c'/></start>"
        "<define name='c'><element name='x'><empty/></element></define>"
        "<define name='c' combine='choice'>"
        "<element name='y'><empty/></element></define>"
        "</grammar>";
    LeptrisRelaxNG rng = leptris_rng_parse(sch, sizeof(sch) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    const char doc[] = "<z/>";
    LeptrisDocument d = leptris_parse_string(doc, sizeof(doc) - 1, &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d), 0);
    EXPECT_NE(leptris_rng_error(rng), nullptr);
    leptris_document_free(d);
    leptris_rng_free(rng);
}

TEST(RngValidate, AttributeValueConstraint) {
    const char* sch =
        "<start><element name='a'>"
        "<attribute name='k'><value>v</value></attribute>"
        "</element></start>";
    EXPECT_EQ(validates(sch, "<a k='v'/>"), 1);
    EXPECT_EQ(validates(sch, "<a k='w'/>"), 0);
}

/* ---- phase 5: <include> via leptris_rng_parse_file -------------- */

#ifndef LEPTRIS_RNG_INCLUDE_DIR
#define LEPTRIS_RNG_INCLUDE_DIR "test/rng/include-cases"
#endif

static LeptrisRelaxNG parse_file(const char* name) {
    LeptrisStatus st = LEPTRIS_OK;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", LEPTRIS_RNG_INCLUDE_DIR, name);
    return leptris_rng_parse_file(path, &st);
}

static int file_validates(const char* schema_name, const char* doc) {
    LeptrisRelaxNG rng = parse_file(schema_name);
    if (!rng) return -1;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(doc, strlen(doc), &st);
    if (!d) {
        leptris_rng_free(rng);
        return -2;
    }
    int ok = leptris_rng_validate(rng, d);
    leptris_document_free(d);
    leptris_rng_free(rng);
    return ok;
}

TEST(RngInclude, MergesDefinesFromIncludedGrammar) {
    EXPECT_EQ(file_validates("main.rng", "<e/>"), 1);
    EXPECT_EQ(file_validates("main.rng", "<x/>"), 0);
}

TEST(RngInclude, NestedDefineOverridesIncluded) {
    EXPECT_EQ(file_validates("override.rng", "<f/>"), 1);
    EXPECT_EQ(file_validates("override.rng", "<e/>"), 0);
}

TEST(RngInclude, NestedStartOverridesIncluded) {
    EXPECT_EQ(file_validates("start-override.rng", "<e/>"), 1);
    EXPECT_EQ(file_validates("start-override.rng", "<g/>"), 0);
}

TEST(RngInclude, OverrideMustTargetSomething) {
    EXPECT_EQ(parse_file("bad-override.rng"), nullptr);
    EXPECT_EQ(parse_file("bad-start-override.rng"), nullptr);
}

TEST(RngInclude, CyclicIncludeTerminates) {
    EXPECT_EQ(parse_file("self-include.rng"), nullptr);
}

TEST(RngInclude, MissingHrefIsAnError) {
    EXPECT_EQ(parse_file("no-href.rng"), nullptr);
}

TEST(RngInclude, ExternalRefLoadsForeignGrammar) {
    /* externalRef: the referenced grammar's start becomes the
     * pattern body (basicdoc.rng's MathML reference). */
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisRelaxNG rng = parse_file("ext-main.rng");
    ASSERT_NE(rng, nullptr) << leptris_last_error();
    const char doc[] = "<r><lib>hi</lib></r>";
    LeptrisDocument d = leptris_parse_string(doc, sizeof(doc) - 1, &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, d), 1);
    const char bad_xml[] = "<r><nope/></r>";
    LeptrisDocument bad = leptris_parse_string(
        bad_xml, strlen(bad_xml), &st);
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_rng_validate(rng, bad), 0);
    leptris_document_free(d);
    leptris_document_free(bad);
    leptris_rng_free(rng);
}

TEST(RngInclude, FileSchemaErrorsPublishDetail) {
    /* The FILE entry must publish schema-parse detail to
     * leptris_last_error like the string entry — it used to free
     * the handle and return NULL with an empty channel, masking
     * every real reason (the metanorma isodoc triage died on
     * this). */
    EXPECT_EQ(parse_file("bad-construct.rng"), nullptr);
    const char* err = leptris_last_error();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(strstr(err, "unknown pattern"), nullptr) << err;
}

TEST(RngInclude, MissingFileIsAnError) {
    EXPECT_EQ(parse_file("does-not-exist.rng"), nullptr);
}

/* ---- phase 6: <param> datatype facets ---------------------------- */

TEST(RngValidate, DataFacetNumericBounds) {
    const char* min_sch =
        "<start><element name='n'><data type='integer'>"
        "<param name='minInclusive'>5</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(min_sch, "<n>5</n>"), 1);
    EXPECT_EQ(validates(min_sch, "<n>4</n>"), 0);
    EXPECT_EQ(validates(min_sch, "<n>10</n>"), 1);
    const char* max_sch =
        "<start><element name='n'><data type='integer'>"
        "<param name='maxInclusive'>10</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(max_sch, "<n>10</n>"), 1);
    EXPECT_EQ(validates(max_sch, "<n>11</n>"), 0);
    const char* minx_sch =
        "<start><element name='n'><data type='integer'>"
        "<param name='minExclusive'>5</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(minx_sch, "<n>5</n>"), 0);
    EXPECT_EQ(validates(minx_sch, "<n>6</n>"), 1);
}

TEST(RngValidate, DataFacetLengthBounds) {
    /* XSD string length counts raw characters (whitespace counts). */
    const char* min_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='minLength'>2</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(min_sch, "<n>ab</n>"), 1);
    EXPECT_EQ(validates(min_sch, "<n>a</n>"), 0);
    EXPECT_EQ(validates(min_sch, "<n> a</n>"), 1);
    const char* len_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='length'>3</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(len_sch, "<n>abc</n>"), 1);
    EXPECT_EQ(validates(len_sch, "<n>ab</n>"), 0);
}

TEST(RngValidate, DataFacetPattern) {
    /* XSD patterns match the WHOLE value. */
    const char* plus_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='pattern'>[ab]+</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(plus_sch, "<n>aba</n>"), 1);
    EXPECT_EQ(validates(plus_sch, "<n>abc</n>"), 0);
    const char* quant_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='pattern'>[0-9]{2,4}</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(quant_sch, "<n>12</n>"), 1);
    EXPECT_EQ(validates(quant_sch, "<n>12345</n>"), 0);
    EXPECT_EQ(validates(quant_sch, "<n>1</n>"), 0);
    const char* alt_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='pattern'>cat|dog</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(alt_sch, "<n>cat</n>"), 1);
    EXPECT_EQ(validates(alt_sch, "<n>catdog</n>"), 0);
    const char* dot_sch =
        "<start><element name='n'><data type='string'>"
        "<param name='pattern'>a.c</param></data>"
        "</element></start>";
    EXPECT_EQ(validates(dot_sch, "<n>abc</n>"), 1);
    EXPECT_EQ(validates(dot_sch, "<n>ac</n>"), 0);
}

TEST(RngValidate, DataFacetsApplyInListAndAttributeContexts) {
    const char* list_sch =
        "<start><element name='l'><list><oneOrMore>"
        "<data type='integer'><param name='maxInclusive'>3</param></data>"
        "</oneOrMore></list></element></start>";
    EXPECT_EQ(validates(list_sch, "<l>1 2 3</l>"), 1);
    EXPECT_EQ(validates(list_sch, "<l>1 5</l>"), 0);
    const char* attr_sch =
        "<start><element name='a'>"
        "<attribute name='k'><data type='string'>"
        "<param name='pattern'>[0-9]+</param></data></attribute>"
        "</element></start>";
    EXPECT_EQ(validates(attr_sch, "<a k='123'/>"), 1);
    EXPECT_EQ(validates(attr_sch, "<a k='x9'/>"), 0);
}

TEST(RngParse, RejectsIllegalParamName) {
    LeptrisStatus st = LEPTRIS_OK;
    const char sch[] = SCHEMA(
        "<start><element name='n'><data type='string'>"
        "<param name='enumeration'>a</param></data>"
        "</element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(sch, sizeof(sch) - 1, &st);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(rng, nullptr);
}

TEST(RngParse, XmlNameClassesInPatterns) {
    /* XSD \i (NameStartChar) and \c (NameChar) escapes - the XML
     * ID pattern "\i\c*|\c+#\c+" appears throughout the
     * metanorma schemas. */
    LeptrisStatus st = LEPTRIS_OK;
    const char schema[] = SCHEMA(
        "<start><element name='r'>"
        "<data type='string'>"
        "<param name='pattern'>\\i\\c*|\\c+#\\c+</param>"
        "</data></element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(schema, sizeof(schema) - 1, &st);
    ASSERT_EQ(st, LEPTRIS_OK);
    ASSERT_NE(rng, nullptr);
    const char good[] = "<r>abc</r>";
    const char good2[] = "<r>a#b</r>";
    const char bad[] = "<r>1ab</r>";
    LeptrisDocument d1 = leptris_parse_string(good, sizeof(good) - 1, NULL);
    LeptrisDocument d2 = leptris_parse_string(good2, sizeof(good2) - 1, NULL);
    LeptrisDocument d3 = leptris_parse_string(bad, sizeof(bad) - 1, NULL);
    ASSERT_TRUE(d1 && d2 && d3);
    EXPECT_EQ(leptris_rng_validate(rng, d1), 1);
    EXPECT_EQ(leptris_rng_validate(rng, d2), 1);
    EXPECT_EQ(leptris_rng_validate(rng, d3), 0);
    leptris_document_free(d1);
    leptris_document_free(d2);
    leptris_document_free(d3);
    leptris_rng_free(rng);
}

TEST(RngParse, RejectsUnsupportedPatternConstruct) {
    LeptrisStatus st = LEPTRIS_OK;
    const char sch[] = SCHEMA(
        "<start><element name='n'><data type='string'>"
        "<param name='pattern'>\\p{L}+</param></data>"
        "</element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(sch, sizeof(sch) - 1, &st);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(rng, nullptr);
}

TEST(RngParse, SchemaErrorsPublishToLastError) {
    LeptrisStatus st = LEPTRIS_OK;
    const char sch[] = SCHEMA(
        "<start><element name='e'><bogus/></element></start>");
    LeptrisRelaxNG rng = leptris_rng_parse(sch, sizeof(sch) - 1, &st);
    EXPECT_EQ(rng, nullptr);
    const char* err = leptris_last_error();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(strstr(err, "unknown pattern"), nullptr) << err;
}

TEST(RngParse, FileErrorsPublishToLastError) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisRelaxNG rng =
        leptris_rng_parse_file("/nonexistent/leptris-spec.rng", &st);
    EXPECT_EQ(rng, nullptr);
    const char* err = leptris_last_error();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(strstr(err, "open"), nullptr) << err;
}
