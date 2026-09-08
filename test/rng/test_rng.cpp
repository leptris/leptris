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
    /* Jing shape: "line:col: error: message". */
    EXPECT_NE(strstr(err, "1:0: error:"), nullptr) << err;
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
