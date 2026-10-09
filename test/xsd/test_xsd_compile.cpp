// test/xsd/test_xsd_compile.cpp — #1075 slice 1: the compilation
// surface. The corpus gate (Libxml2Corpus) walks every vendored
// libxml2 test/schemas fixture through leptris_xsd_compile: no
// crashes, clean statuses, and a compile-success floor pinned from
// the first run — regressions drop the count, later slices raise
// it (the red-list records why each remaining red is red).
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

#include "leptris.h"

#ifdef _WIN32
#include <windows.h>
static std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}
#else
#include <dirent.h>
static std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) out.push_back(e->d_name);
    closedir(d);
    return out;
}
#endif

namespace {

std::string slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

std::vector<std::string> fixture_dir(const char* sub) {
#ifndef LEPTRIS_XSD_CASES_DIR
#define LEPTRIS_XSD_CASES_DIR "libxml2-cases"
#endif
    const char* root = LEPTRIS_XSD_CASES_DIR;
    std::string path = std::string(root) + "/" + sub;
    std::vector<std::string> out;
    for (const std::string& name : list_dir(path))
        if (std::strstr(name.c_str(), ".xsd"))
            out.push_back(path + "/" + name);
    std::sort(out.begin(), out.end());
    return out;
}

TEST(XsdCompile, MinimalSchemaCountsDeclarations) {
    const char* xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:element name=\"a\"/>"
        "<xs:complexType name=\"t\"/>"
        "<xs:simpleType name=\"s\"/>"
        "</xs:schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_OK);
    EXPECT_EQ(leptris_xsd_declaration_count(s), 3u);
    EXPECT_EQ(leptris_xsd_error(s), nullptr);
    leptris_xsd_free(s);
}

TEST(XsdCompile, DefaultNamespaceSpellingBinds) {
    const char* xsd =
        "<schema xmlns=\"http://www.w3.org/2001/XMLSchema\">"
        "<element name=\"a\"/>"
        "</schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_OK);
    EXPECT_EQ(leptris_xsd_declaration_count(s), 1u);
    leptris_xsd_free(s);
}

TEST(XsdCompile, NonSchemaRootCarriesTheError) {
    const char* not_schema = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(not_schema, strlen(not_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_NE(leptris_xsd_error(s), nullptr);
    leptris_xsd_free(s);
}

TEST(XsdCompile, MalformedTextIsACleanParseError) {
    const char* broken = "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(broken, strlen(broken), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    leptris_xsd_free(s);
}

TEST(XsdCompile, NullTolerant) {
    LeptrisStatus st = LEPTRIS_OK;
    EXPECT_EQ(leptris_xsd_compile(NULL, 0, &st), (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_ERROR_PARSE);
    EXPECT_EQ(leptris_xsd_declaration_count(NULL), 0u);
    leptris_xsd_free(NULL);
    EXPECT_EQ(leptris_xsd_error(NULL), nullptr);
}

// The differential gate: every vendored fixture compiles or fails
// CLEANLY; the success floor is pinned from the first measured run
// (slice 1). Raising it is progress; lowering it is a regression.
TEST(Libxml2Corpus, EveryFixtureCompilesOrFailsCleanly) {
    size_t ok = 0, failed = 0;
    for (const std::string& path : fixture_dir(".")) {
        LeptrisStatus st = (LeptrisStatus)0;
        /* compile_file: xs:include/xs:import schemaLocations
         * resolve against the fixture's own directory */
        LeptrisXsdSchema s = leptris_xsd_compile_file(path.c_str(), &st);
        ASSERT_NE(s, (LeptrisXsdSchema)0) << path;
        if (st == LEPTRIS_OK)
            ok++;
        else
            failed++;
        leptris_xsd_free(s);
    }
    EXPECT_GE(ok + failed, 160u) << "fixture set shrank";
    // Slice-1 floor: every vendored fixture's schema root compiles
    // (163/163 on the first run). Lowering this is a regression;
    // later slices keep it monotonically rising.
    RecordProperty("ok", std::to_string(ok));
    RecordProperty("failed", std::to_string(failed));
    EXPECT_GE(ok, 160u) << "compile-success floor regressed";
}

}  // namespace

// ---- slice 2: lexical validation ----------------------------------
namespace {

struct BuiltinCase {
    const char* type;
    const char* value;
    int expect; /* 1 valid, 0 invalid, -1 not-in-table */
};

TEST(XsdDatatypes, BuiltinsLexicalMatrix) {
    const BuiltinCase cases[] = {
        {"xs:integer", "42", 1},        {"xs:integer", "-7", 1},
        {"xs:integer", "+007", 1},       {"xs:integer", "3.5", 0},
        {"xs:integer", "", 0},           {"xs:integer", "12a", 0},
        {"xs:byte", "127", 1},           {"xs:byte", "128", 0},
        {"xs:unsignedInt", "0", 1},      {"xs:unsignedInt", "-1", 0},
        {"xs:positiveInteger", "1", 1},  {"xs:positiveInteger", "0", 0},
        {"xs:decimal", "-0.5", 1},       {"xs:decimal", ".5", 1},
        {"xs:decimal", "5.", 1},         {"xs:decimal", ".", 0},
        {"xs:boolean", "true", 1},       {"xs:boolean", "2", 0},
        {"xs:double", "1.5e10", 1},      {"xs:double", "INF", 1},
        {"xs:date", "2026-10-08", 1},    {"xs:date", "2026-13-01", 0},
        {"xs:dateTime", "2026-10-08T12:30:00", 1},
        {"xs:time", "23:59:59.123", 1},  {"xs:time", "24:00:00", 1},
        {"xs:duration", "P1Y2M3DT4H5M6S", 1},
        {"xs:duration", "P", 0},
        {"xs:hexBinary", "0A1F", 1},     {"xs:hexBinary", "0A1", 0},
        {"xs:base64Binary", "QUJD", 1},  {"xs:base64Binary", "!", 0},
        {"xs:NCName", "_a1", 1},         {"xs:NCName", "1a", 0},
        {"xs:token", "a b", 1},          {"xs:token", "a  b", 0},
        {"xs:language", "en", 1},        {"xs:language", "e!", 0},
        {"xs:anyURI", "http://x/y", 1},
        {"xs:notAThing", "x", -1},
    };
    for (const BuiltinCase& c : cases) {
        SCOPED_TRACE(std::string(c.type) + " : " + c.value);
        EXPECT_EQ(leptris_xsd_builtin_valid(c.type, c.value), c.expect)
            << c.type << " : " << c.value;
    }
}

TEST(XsdDatatypes, RestrictionFacetsApply) {
    const char* xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:simpleType name=\"count\">"
        "<xs:restriction base=\"xs:integer\">"
        "<xs:minInclusive value=\"1\"/>"
        "<xs:maxInclusive value=\"10\"/>"
        "</xs:restriction></xs:simpleType>"
        "<xs:simpleType name=\"code\">"
        "<xs:restriction base=\"xs:string\">"
        "<xs:pattern value=\"[A-Z]{3}\"/>"
        "</xs:restriction></xs:simpleType>"
        "<xs:simpleType name=\"color\">"
        "<xs:restriction base=\"xs:string\">"
        "<xs:enumeration value=\"red\"/>"
        "<xs:enumeration value=\"green\"/>"
        "</xs:restriction></xs:simpleType>"
        "<xs:simpleType name=\"label\">"
        "<xs:restriction base=\"xs:string\">"
        "<xs:minLength value=\"2\"/>"
        "<xs:maxLength value=\"4\"/>"
        "</xs:restriction></xs:simpleType>"
        "</xs:schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(st, LEPTRIS_OK);

    EXPECT_EQ(leptris_xsd_simple_valid(s, "count", "5"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "count", "0"), 0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "count", "11"), 0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "count", "x"), 0);

    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "ABC"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "ABCD"), 0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "abc"), 0);

    EXPECT_EQ(leptris_xsd_simple_valid(s, "color", "red"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "color", "blue"), 0);

    EXPECT_EQ(leptris_xsd_simple_valid(s, "label", "ab"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "label", "abcde"), 0);

    EXPECT_EQ(leptris_xsd_simple_valid(s, "missing", "x"), -1);
    leptris_xsd_free(s);
}

TEST(XsdDatatypes, LocalBaseChainValidatesEveryHop) {
    const char* xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:simpleType name=\"outer\">"
        "<xs:restriction base=\"inner\">"
        "<xs:maxInclusive value=\"100\"/>"
        "</xs:restriction></xs:simpleType>"
        "<xs:simpleType name=\"inner\">"
        "<xs:restriction base=\"xs:integer\">"
        "<xs:minInclusive value=\"10\"/>"
        "</xs:restriction></xs:simpleType>"
        "</xs:schema>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    /* Both hops' facets bind: 5 fails inner, 150 fails outer. */
    EXPECT_EQ(leptris_xsd_simple_valid(s, "outer", "50"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "outer", "5"), 0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "outer", "150"), 0);
    leptris_xsd_free(s);
}

}  // namespace

// ---- slice 3: content models ---------------------------------------
namespace {

const char* k_cm_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\""
    " targetNamespace=\"urn:t\">"
    "<xs:complexType name=\"pairT\">"
    "<xs:sequence>"
    "<xs:element name=\"a\"/>"
    "<xs:element name=\"b\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"pair\" type=\"pairT\"/>"

    "<xs:complexType name=\"pickT\">"
    "<xs:choice>"
    "<xs:element name=\"x\"/>"
    "<xs:element name=\"y\"/>"
    "</xs:choice></xs:complexType>"
    "<xs:element name=\"pick\" type=\"pickT\"/>"

    "<xs:complexType name=\"twoToThreeT\">"
    "<xs:sequence>"
    "<xs:element name=\"a\" minOccurs=\"2\" maxOccurs=\"3\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"twoToThree\" type=\"twoToThreeT\"/>"

    "<xs:complexType name=\"listT\">"
    "<xs:sequence>"
    "<xs:element name=\"item\" maxOccurs=\"unbounded\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"list\" type=\"listT\"/>"

    "<xs:complexType name=\"looseT\">"
    "<xs:sequence>"
    "<xs:any namespace=\"##other\" processContents=\"skip\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"loose\" type=\"looseT\"/>"

    "<xs:complexType name=\"nestedT\">"
    "<xs:sequence>"
    "<xs:choice>"
    "<xs:element name=\"x\"/>"
    "<xs:element name=\"y\"/>"
    "</xs:choice>"
    "<xs:element name=\"c\"/>"
    "</xs:sequence></xs:complexType>"
    "<xs:element name=\"nested\" type=\"nestedT\"/>"

    "<xs:element name=\"text\" type=\"xs:string\"/>"
    "</xs:schema>";

LeptrisXsdSchema compile_cm() {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_cm_schema, strlen(k_cm_schema), &st);
    EXPECT_EQ(st, LEPTRIS_OK);
    return s;
}

TEST(XsdContent, SequenceInOrder) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* ab[] = {"a", "b"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "pair", ab, NULL, 2), 1);
    const char* ba[] = {"b", "a"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "pair", ba, NULL, 2), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "pair", ab, NULL, 1), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "pair", NULL, NULL, 0), 0);
    leptris_xsd_free(s);
}

TEST(XsdContent, ChoicePicksExactlyOne) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* x[] = {"x"};
    const char* y[] = {"y"};
    const char* xy[] = {"x", "y"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "pick", x, NULL, 1), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "pick", y, NULL, 1), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "pick", xy, NULL, 2), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "pick", NULL, NULL, 0), 0);
    leptris_xsd_free(s);
}

TEST(XsdContent, OccurrenceBoundsHold) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* a1[] = {"a"};
    const char* a2[] = {"a", "a"};
    const char* a3[] = {"a", "a", "a"};
    const char* a4[] = {"a", "a", "a", "a"};
    SCOPED_TRACE("a1");
    EXPECT_EQ(leptris_xsd_content_valid(s, "twoToThree", a1, NULL, 1), 0);
    SCOPED_TRACE("a2");
    EXPECT_EQ(leptris_xsd_content_valid(s, "twoToThree", a2, NULL, 2), 1);
    SCOPED_TRACE("a3");
    EXPECT_EQ(leptris_xsd_content_valid(s, "twoToThree", a3, NULL, 3), 1);
    SCOPED_TRACE("a4");
    EXPECT_EQ(leptris_xsd_content_valid(s, "twoToThree", a4, NULL, 4), 0);
    leptris_xsd_free(s);
}

TEST(XsdContent, UnboundedRepeats) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* i3[] = {"item", "item", "item"};
    const char* i30[30];
    for (int i = 0; i < 30; i++) i30[i] = "item";
    EXPECT_EQ(leptris_xsd_content_valid(s, "list", NULL, NULL, 0), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "list", i3, NULL, 3), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "list", i30, NULL, 30), 1);
    leptris_xsd_free(s);
}

TEST(XsdContent, WildcardNamespaceFilter) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    /* ##other: anything qualified OUTSIDE urn:t */
    const char* w[] = {"widget"};
    const char* tns[] = {"urn:t"};
    const char* other[] = {"urn:elsewhere"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "loose", w, other, 1), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "loose", w, tns, 1), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "loose", w, NULL, 1), 0);
    leptris_xsd_free(s);
}

TEST(XsdContent, NestedGroupsCompose) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* xc[] = {"x", "c"};
    const char* yc[] = {"y", "c"};
    const char* cc[] = {"c", "c"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "nested", xc, NULL, 2), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "nested", yc, NULL, 2), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "nested", cc, NULL, 2), 0);
    leptris_xsd_free(s);
}

TEST(XsdContent, SimpleTypedElementTakesNoChildren) {
    LeptrisXsdSchema s = compile_cm();
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* x[] = {"x"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "text", NULL, NULL, 0), 1);
    EXPECT_EQ(leptris_xsd_content_valid(s, "text", x, NULL, 1), 0);
    EXPECT_EQ(leptris_xsd_content_valid(s, "noSuchElement", x, NULL, 1),
              -1);
    leptris_xsd_free(s);
}

}  // namespace

// ---- slice 4: instance validation ----------------------------------
namespace {

const char* k_v_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"rowT\">"
    "<xs:sequence>"
    "<xs:element name=\"id\" type=\"xs:integer\"/>"
    "<xs:element name=\"name\" type=\"xs:string\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"status\" type=\"xs:string\"/>"
    "<xs:attribute name=\"version\" type=\"xs:integer\""
    " use=\"required\"/>"
    "</xs:complexType>"
    "<xs:element name=\"rows\" type=\"rowT\"/>"
    "<xs:element name=\"count\" type=\"xs:positiveInteger\"/>"
    "</xs:schema>";

int validate_doc(const char* schema, const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(schema, strlen(schema), &st);
    if (!s) return -2;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    if (!d) {
        leptris_xsd_free(s);
        return -3;
    }
    int r = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return r;
}

TEST(XsdValidate, ValidDocumentPasses) {
    EXPECT_EQ(validate_doc(k_v_schema,
                           "<rows version=\"2\" status=\"ok\">"
                           "<id>7</id><name>abc</name></rows>"), 1);
    EXPECT_EQ(validate_doc(k_v_schema, "<count>3</count>"), 1);
}

TEST(XsdValidate, ContentModelViolationReports) {
    EXPECT_EQ(validate_doc(k_v_schema,
                           "<rows version=\"1\">"
                           "<name>abc</name><id>7</id></rows>"), 0);
}

TEST(XsdValidate, RequiredAttributeAbsentReports) {
    int r = validate_doc(k_v_schema, "<rows><id>7</id><name>a</name></rows>");
    EXPECT_EQ(r, 0);
}

TEST(XsdValidate, AttributeTypeViolationReports) {
    EXPECT_EQ(validate_doc(k_v_schema,
                           "<rows version=\"x\"><id>7</id>"
                           "<name>a</name></rows>"), 0);
}

TEST(XsdValidate, ElementTextTypeViolationReports) {
    EXPECT_EQ(validate_doc(k_v_schema, "<count>-2</count>"), 0);
    EXPECT_EQ(validate_doc(k_v_schema, "<count>0</count>"), 0);
}

TEST(XsdValidate, ChildElementTypeViolationReports) {
    EXPECT_EQ(validate_doc(k_v_schema,
                           "<rows version=\"1\">"
                           "<id>nan</id><name>a</name></rows>"), 0);
}

TEST(XsdValidate, ErrorsEnumerate) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_v_schema, strlen(k_v_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* bad = "<rows><id>nan</id><name>a</name></rows>";
    LeptrisDocument d = leptris_parse_string(bad, strlen(bad), &st);
    ASSERT_NE(d, nullptr);
    int r = leptris_xsd_validate(s, d);
    EXPECT_EQ(r, 0);
    EXPECT_GE(leptris_xsd_error_count(s), 1u);
    const char* first = leptris_xsd_error_at(s, 0);
    EXPECT_NE(first, nullptr);
    EXPECT_EQ(leptris_xsd_error_at(s, 99), nullptr);
    leptris_document_free(d);
    leptris_xsd_free(s);
}

TEST(XsdValidate, NullTolerant) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_v_schema, strlen(k_v_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(leptris_xsd_validate(NULL, NULL), -1);
    EXPECT_EQ(leptris_xsd_validate(s, NULL), -1);
    EXPECT_EQ(leptris_xsd_error_count(NULL), 0u);
    EXPECT_EQ(leptris_xsd_error_at(NULL, 0), nullptr);
    leptris_xsd_free(s);
}

}  // namespace

// ---- #1592: inline anonymous types ---------------------------------
namespace {

const char* k_anon_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:element name=\"book\">"
    "<xs:complexType>"
    "<xs:sequence>"
    "<xs:element name=\"title\" type=\"xs:token\"/>"
    "<xs:element name=\"author\" type=\"xs:token\"/>"
    "</xs:sequence>"
    "</xs:complexType>"
    "</xs:element>"
    "</xs:schema>";

TEST(XsdContent, AnonymousInlineComplexTypeCaptured) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_anon_schema, strlen(k_anon_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    /* the issue's exact symptom: content_valid answered -1 */
    const char* good[] = {"title", "author"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "book", good, NULL, 2), 1);
    const char* bad[] = {"author", "author", "isbn"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "book", bad, NULL, 3), 0);
    const char* miss[] = {"author"};
    EXPECT_EQ(leptris_xsd_content_valid(s, "book", miss, NULL, 1), 0);
    leptris_xsd_free(s);
}

TEST(XsdValidate, AnonymousInlineValidatesEndToEnd) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_anon_schema, strlen(k_anon_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* good_x = "<book><title>t</title><author>a</author></book>";
    LeptrisDocument good =
        leptris_parse_string(good_x, strlen(good_x), &st);
    ASSERT_NE(good, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, good), 1);
    leptris_document_free(good);
    /* the issue's instance: missing title, out-of-model isbn */
    const char* bad_x =
        "<book><author/><author/><isbn>9</isbn></book>";
    LeptrisDocument bad = leptris_parse_string(bad_x, strlen(bad_x), &st);
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, bad), 0);
    EXPECT_GE(leptris_xsd_error_count(s), 1u);
    leptris_document_free(bad);
    leptris_xsd_free(s);
}

TEST(XsdDatatypes, AnonymousInlineSimpleTypeOnParticle) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:complexType name=\"t\">"
        "<xs:sequence>"
        "<xs:element name=\"score\">"
        "<xs:simpleType>"
        "<xs:restriction base=\"xs:integer\">"
        "<xs:minInclusive value=\"1\"/>"
        "</xs:restriction>"
        "</xs:simpleType>"
        "</xs:element>"
        "</xs:sequence></xs:complexType>"
        "<xs:element name=\"game\" type=\"t\"/>"
        "</xs:schema>";
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* good_x = "<game><score>9</score></game>";
    LeptrisDocument good = leptris_parse_string(good_x, strlen(good_x), &st);
    ASSERT_NE(good, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, good), 1);
    leptris_document_free(good);
    const char* bad_x = "<game><score>0</score></game>";
    LeptrisDocument bad = leptris_parse_string(bad_x, strlen(bad_x), &st);
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, bad), 0);
    leptris_document_free(bad);
    /* the synthesized slot resolves directly too */
    EXPECT_EQ(leptris_xsd_simple_valid(s, "element:score", "9"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "element:score", "0"), 0);
    leptris_xsd_free(s);
}

}  // namespace

// ---- slice 6: identity constraints ---------------------------------
namespace {

const char* k_ic_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:element name=\"catalog\">"
    "<xs:complexType>"
    "<xs:sequence>"
    "<xs:element name=\"product\" maxOccurs=\"unbounded\">"
    "<xs:complexType>"
    "<xs:sequence>"
    "<xs:element name=\"partNum\" type=\"xs:string\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/>"
    "</xs:complexType>"
    "</xs:element>"
    "</xs:sequence>"
    "</xs:complexType>"
    "<xs:key name=\"productId\">"
    "<xs:selector xpath=\"product\"/>"
    "<xs:field xpath=\"@id\"/>"
    "</xs:key>"
    "<xs:keyref name=\"productRef\" refer=\"productId\">"
    "<xs:selector xpath=\"product\"/>"
    "<xs:field xpath=\"partNum\"/>"
    "</xs:keyref>"
    "</xs:element>"
    "</xs:schema>";

TEST(XsdIc, UniqueKeysValidate) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_ic_schema, strlen(k_ic_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* xml =
        "<catalog>"
        "<product id=\"A1\"><partNum>A1</partNum></product>"
        "<product id=\"B2\"><partNum>B2</partNum></product>"
        "</catalog>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 1)
        << (leptris_xsd_error_count(s) ? leptris_xsd_error_at(s, 0) : "");
    leptris_document_free(d);
    leptris_xsd_free(s);
}

TEST(XsdIc, DuplicateKeyReports) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_ic_schema, strlen(k_ic_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* xml =
        "<catalog>"
        "<product id=\"A1\"><partNum>A1</partNum></product>"
        "<product id=\"A1\"><partNum>zz</partNum></product>"
        "</catalog>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0);
    size_t n = leptris_xsd_error_count(s);
    int saw_dup = 0;
    for (size_t i = 0; i < n; i++) {
        const char* e = leptris_xsd_error_at(s, i);
        if (e && strstr(e, "duplicate")) saw_dup = 1;
    }
    EXPECT_TRUE(saw_dup);
    leptris_document_free(d);
    leptris_xsd_free(s);
}

TEST(XsdIc, UnresolvedKeyrefReports) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_ic_schema, strlen(k_ic_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* xml =
        "<catalog>"
        "<product id=\"A1\"><partNum>NOPE</partNum></product>"
        "</catalog>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0);
    size_t n = leptris_xsd_error_count(s);
    int saw_ref = 0;
    for (size_t i = 0; i < n; i++) {
        const char* e = leptris_xsd_error_at(s, i);
        if (e && strstr(e, "keyref")) saw_ref = 1;
    }
    EXPECT_TRUE(saw_ref);
    leptris_document_free(d);
    leptris_xsd_free(s);
}

TEST(XsdIc, KeyRequiresFieldValues) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xsd =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:element name=\"r\">"
        "<xs:complexType><xs:sequence>"
        "<xs:element name=\"item\" maxOccurs=\"unbounded\"/>"
        "</xs:sequence></xs:complexType>"
        "<xs:key name=\"k\">"
        "<xs:selector xpath=\"item\"/>"
        "<xs:field xpath=\"@id\"/>"
        "</xs:key>"
        "</xs:element></xs:schema>";
    LeptrisXsdSchema s = leptris_xsd_compile(xsd, strlen(xsd), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    const char* xml = "<r><item/><item id=\"a\"/></r>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 0); /* missing field */
    leptris_document_free(d);
    leptris_xsd_free(s);
}

}  // namespace

// ---- local particle declarations -----------------------------------
namespace {

/* item is declared ONLY as a particle of orderT — no global
 * xs:element named item exists — so its attributes and children
 * must validate against itemT's rows through the particle's type. */
const char* k_local_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"itemT\">"
    "<xs:sequence>"
    "<xs:element name=\"sku\" type=\"xs:token\"/>"
    "<xs:element name=\"qty\" type=\"xs:integer\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"id\" type=\"xs:integer\" use=\"required\"/>"
    "</xs:complexType>"
    "<xs:complexType name=\"orderT\">"
    "<xs:sequence>"
    "<xs:element name=\"item\" type=\"itemT\" maxOccurs=\"unbounded\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"ref\" type=\"xs:string\"/>"
    "</xs:complexType>"
    "<xs:element name=\"order\" type=\"orderT\"/>"
    "</xs:schema>";

TEST(XsdLocal, LocalChildAttributesValidate) {
    EXPECT_EQ(validate_doc(k_local_schema,
                           "<order><item id=\"5\"><sku>a</sku>"
                           "<qty>2</qty></item></order>"), 1);
    EXPECT_EQ(validate_doc(k_local_schema,
                           "<order><item id=\"nan\"><sku>a</sku>"
                           "<qty>2</qty></item></order>"), 0);
    EXPECT_EQ(validate_doc(k_local_schema,
                           "<order><item><sku>a</sku>"
                           "<qty>2</qty></item></order>"), 0);
}

TEST(XsdLocal, LocalChildContentModelValidates) {
    EXPECT_EQ(validate_doc(k_local_schema,
                           "<order><item id=\"5\"><qty>2</qty>"
                           "<sku>a</sku></item></order>"), 0);
    EXPECT_EQ(validate_doc(k_local_schema,
                           "<order><item id=\"5\"><sku>a</sku>"
                           "<qty>nan</qty></item></order>"), 0);
}

}  // namespace

// ---- tier-1 charter: union / list simple-type derivation -----------
namespace {

const char* k_ul_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:simpleType name=\"ints\">"
    "<xs:list itemType=\"xs:integer\"/>"
    "</xs:simpleType>"
    "<xs:simpleType name=\"alpha\">"
    "<xs:restriction base=\"xs:string\">"
    "<xs:pattern value=\"[a-z]+\"/>"
    "</xs:restriction>"
    "</xs:simpleType>"
    "<xs:simpleType name=\"code\">"
    "<xs:union memberTypes=\"xs:integer alpha\"/>"
    "</xs:simpleType>"
    "<xs:element name=\"vals\" type=\"ints\"/>"
    "<xs:element name=\"c\" type=\"code\"/>"
    "</xs:schema>";

TEST(XsdDatatypes, ListDerivationValidatesEveryItem) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_ul_schema, strlen(k_ul_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "ints", "1 2 3"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "ints", "1 x 3"), 0);
    leptris_xsd_free(s);
}

TEST(XsdDatatypes, UnionDerivationAcceptsAnyMember) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s =
        leptris_xsd_compile(k_ul_schema, strlen(k_ul_schema), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "42"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "abc"), 1);
    EXPECT_EQ(leptris_xsd_simple_valid(s, "code", "42x"), 0);
    leptris_xsd_free(s);
}

TEST(XsdValidate, UnionAndListValidateInDocuments) {
    EXPECT_EQ(validate_doc(k_ul_schema, "<vals>1 2 3</vals>"), 1);
    EXPECT_EQ(validate_doc(k_ul_schema, "<vals>1 x</vals>"), 0);
    EXPECT_EQ(validate_doc(k_ul_schema, "<c>abc</c>"), 1);
    EXPECT_EQ(validate_doc(k_ul_schema, "<c>99</c>"), 1);
    EXPECT_EQ(validate_doc(k_ul_schema, "<c>1.5</c>"), 0);
}

/* inline anonymous union member (no memberTypes for it) */
TEST(XsdDatatypes, UnionInlineAnonymousMember) {
    const char* x =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:simpleType name=\"combo\">"
        "<xs:union memberTypes=\"xs:integer\">"
        "<xs:simpleType><xs:restriction base=\"xs:string\">"
        "<xs:enumeration value=\"yes\"/>"
        "<xs:enumeration value=\"no\"/>"
        "</xs:restriction></xs:simpleType>"
        "</xs:union>"
        "</xs:simpleType>"
        "<xs:element name=\"q\" type=\"combo\"/>"
        "</xs:schema>";
    EXPECT_EQ(validate_doc(x, "<q>3</q>"), 1);
    EXPECT_EQ(validate_doc(x, "<q>yes</q>"), 1);
    EXPECT_EQ(validate_doc(x, "<q>maybe</q>"), 0);
}


}  // namespace

// ---- tier-1 charter: named groups + attribute groups + attr refs --
namespace {

const char* k_grp_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:group name=\"meta\">"
    "<xs:sequence>"
    "<xs:element name=\"title\" type=\"xs:token\"/>"
    "<xs:element name=\"date\" type=\"xs:date\"/>"
    "</xs:sequence>"
    "</xs:group>"
    "<xs:attribute name=\"rev\" type=\"xs:integer\"/>"
    "<xs:attributeGroup name=\"common\">"
    "<xs:attribute name=\"id\" type=\"xs:integer\" use=\"required\"/>"
    "<xs:attribute name=\"lang\" type=\"xs:string\"/>"
    "</xs:attributeGroup>"
    "<xs:complexType name=\"docT\">"
    "<xs:sequence>"
    "<xs:group ref=\"meta\"/>"
    "<xs:element name=\"body\" type=\"xs:string\"/>"
    "</xs:sequence>"
    "<xs:attributeGroup ref=\"common\"/>"
    "<xs:attribute ref=\"rev\"/>"
    "</xs:complexType>"
    "<xs:element name=\"doc\" type=\"docT\"/>"
    "</xs:schema>";

TEST(XsdContent, NamedModelGroupSplices) {
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"1\" lang=\"en\" rev=\"2\">"
                           "<title>t</title><date>2026-01-01</date>"
                           "<body>b</body></doc>"), 1);
    /* body before the spliced group content */
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"1\"><body>b</body>"
                           "<title>t</title><date>2026-01-01</date>"
                           "</doc>"), 0);
    /* missing a group member */
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"1\"><title>t</title>"
                           "<body>b</body></doc>"), 0);
    /* group member text types ride the splice */
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"1\"><title>t</title>"
                           "<date>not-a-date</date><body>b</body>"
                           "</doc>"), 0);
}

TEST(XsdValidate, AttributeGroupRowsApply) {
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc><title>t</title>"
                           "<date>2026-01-01</date><body>b</body></doc>"), 0);
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"x\"><title>t</title>"
                           "<date>2026-01-01</date><body>b</body></doc>"),
              0);
    /* the referenced top-level attribute enforces its type */
    EXPECT_EQ(validate_doc(k_grp_schema,
                           "<doc id=\"1\" rev=\"x\"><title>t</title>"
                           "<date>2026-01-01</date><body>b</body></doc>"),
              0);
}

}  // namespace

// ---- tier-1 charter: complexContent / simpleContent derivation -----
namespace {

const char* k_deriv_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"baseT\">"
    "<xs:sequence>"
    "<xs:element name=\"a\" type=\"xs:token\"/>"
    "<xs:element name=\"b\" type=\"xs:token\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"x\" type=\"xs:integer\"/>"
    "</xs:complexType>"
    "<xs:complexType name=\"extT\">"
    "<xs:complexContent>"
    "<xs:extension base=\"baseT\">"
    "<xs:sequence>"
    "<xs:element name=\"c\" type=\"xs:integer\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"y\" type=\"xs:string\"/>"
    "</xs:extension>"
    "</xs:complexContent>"
    "</xs:complexType>"
    "<xs:complexType name=\"rstrT\">"
    "<xs:complexContent>"
    "<xs:restriction base=\"baseT\">"
    "<xs:sequence>"
    "<xs:element name=\"a\" type=\"xs:token\"/>"
    "</xs:sequence>"
    "</xs:restriction>"
    "</xs:complexContent>"
    "</xs:complexType>"
    "<xs:complexType name=\"labelT\">"
    "<xs:simpleContent>"
    "<xs:extension base=\"xs:string\">"
    "<xs:attribute name=\"lang\" type=\"xs:string\" use=\"required\"/>"
    "</xs:extension>"
    "</xs:simpleContent>"
    "</xs:complexType>"
    "<xs:element name=\"e\" type=\"extT\"/>"
    "<xs:element name=\"r\" type=\"rstrT\"/>"
    "<xs:element name=\"l\" type=\"labelT\"/>"
    "</xs:schema>";

TEST(XsdValidate, ExtensionSplicesBaseContentAndAttrs) {
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<e x=\"1\" y=\"s\"><a>t</a><b>u</b>"
                           "<c>3</c></e>"), 1);
    /* base child still required after extension */
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<e x=\"1\" y=\"s\"><a>t</a>"
                           "<c>3</c></e>"), 0);
    /* derived child appends after the base sequence */
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<e x=\"1\" y=\"s\"><c>3</c><a>t</a>"
                           "<b>u</b></e>"), 0);
    /* base attribute rows ride the derivation */
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<e x=\"z\" y=\"s\"><a>t</a><b>u</b>"
                           "<c>3</c></e>"), 0);
    /* derived-only attribute optional */
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<e x=\"1\"><a>t</a><b>u</b>"
                           "<c>3</c></e>"), 1);
}

TEST(XsdValidate, RestrictionReplacesContentModel) {
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<r x=\"1\"><a>t</a></r>"), 1);
    /* the restricted-away child is no longer allowed */
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<r x=\"1\"><a>t</a><b>u</b></r>"), 0);
}

TEST(XsdValidate, SimpleContentTextAndAttrs) {
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<l lang=\"en\">hello</l>"), 1);
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<l>hello</l>"), 0);
    EXPECT_EQ(validate_doc(k_deriv_schema,
                           "<l lang=\"en\">42</l>"), 1);
}

}  // namespace


// ---- tier-1 charter: fixed/default + substitutionGroup -------------
namespace {

const char* k_fd_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"orderT\">"
    "<xs:sequence>"
    "<xs:element name=\"item\" type=\"xs:token\"/>"
    "</xs:sequence>"
    "<xs:attribute name=\"country\" type=\"xs:string\" fixed=\"US\"/>"
    "<xs:attribute name=\"prio\" type=\"xs:integer\" default=\"2\"/>"
    "</xs:complexType>"
    "<xs:element name=\"order\" type=\"orderT\"/>"
    "<xs:element name=\"ship\" type=\"xs:string\" fixed=\"air\"/>"
    "</xs:schema>";

TEST(XsdValidate, FixedAttributeValueEnforced) {
    EXPECT_EQ(validate_doc(k_fd_schema,
                           "<order country=\"US\"><item>x</item>"
                           "</order>"), 1);
    EXPECT_EQ(validate_doc(k_fd_schema,
                           "<order country=\"US\" prio=\"7\">"
                           "<item>x</item></order>"), 1);
    EXPECT_EQ(validate_doc(k_fd_schema,
                           "<order country=\"DE\"><item>x</item>"
                           "</order>"), 0);
    EXPECT_EQ(validate_doc(k_fd_schema,
                           "<order><item>x</item></order>"), 1);
}

TEST(XsdValidate, FixedElementTextEnforced) {
    EXPECT_EQ(validate_doc(k_fd_schema, "<ship>air</ship>"), 1);
    EXPECT_EQ(validate_doc(k_fd_schema, "<ship>sea</ship>"), 0);
}

const char* k_subst_schema =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
    "<xs:complexType name=\"boxT\">"
    "<xs:sequence>"
    "<xs:element name=\"head\" type=\"xs:token\"/>"
    "</xs:sequence>"
    "</xs:complexType>"
    "<xs:element name=\"head\" type=\"xs:token\"/>"
    "<xs:element name=\"alt\" type=\"xs:token\" substitutionGroup=\"head\"/>"
    "<xs:element name=\"box\" type=\"boxT\"/>"
    "</xs:schema>";

TEST(XsdValidate, SubstitutionGroupMembersBind) {
    EXPECT_EQ(validate_doc(k_subst_schema,
                           "<box><head>x</head></box>"), 1);
    EXPECT_EQ(validate_doc(k_subst_schema,
                           "<box><alt>y</alt></box>"), 1);
    EXPECT_EQ(validate_doc(k_subst_schema,
                           "<box><other>z</other></box>"), 0);
}

}  // namespace

// ---- tier-1 charter: include/import via compile_file ---------------
namespace {

TEST(XsdCompileFile, IncludeResolvesRelativeSchemaLocation) {
    std::string dir = ::testing::TempDir();
    std::string common = dir + "xsdcommon.xsd";
    std::string main = dir + "xsdmain.xsd";
    FILE* f1 = fopen(common.c_str(), "wb");
    ASSERT_NE(f1, nullptr);
    const char* common_text =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:complexType name=\"itemT\">"
        "<xs:sequence>"
        "<xs:element name=\"sku\" type=\"xs:token\"/>"
        "</xs:sequence>"
        "<xs:attribute name=\"id\" type=\"xs:integer\" use=\"required\"/>"
        "</xs:complexType>"
        "</xs:schema>";
    fwrite(common_text, 1, strlen(common_text), f1);
    fclose(f1);
    FILE* f2 = fopen(main.c_str(), "wb");
    ASSERT_NE(f2, nullptr);
    const char* main_text =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
        "<xs:include schemaLocation=\"xsdcommon.xsd\"/>"
        "<xs:complexType name=\"listT\">"
        "<xs:sequence>"
        "<xs:element name=\"item\" type=\"itemT\""
        " maxOccurs=\"unbounded\"/>"
        "</xs:sequence>"
        "</xs:complexType>"
        "<xs:element name=\"list\" type=\"listT\"/>"
        "</xs:schema>";
    fwrite(main_text, 1, strlen(main_text), f2);
    fclose(f2);

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisXsdSchema s = leptris_xsd_compile_file(main.c_str(), &st);
    ASSERT_NE(s, (LeptrisXsdSchema)0);
    /* the included declaration participates: itemT's rows type the
     * particle's attributes and its content model applies */
    const char* good =
        "<list><item id=\"5\"><sku>a</sku></item></list>";
    LeptrisDocument d =
        leptris_parse_string(good, strlen(good), &st);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, d), 1);
    const char* badx =
        "<list><item id=\"x\"><sku>a</sku></item></list>";
    LeptrisDocument bad =
        leptris_parse_string(badx, strlen(badx), &st);
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(leptris_xsd_validate(s, bad), 0);
    leptris_document_free(d);
    leptris_document_free(bad);
    leptris_xsd_free(s);
    remove(common.c_str());
    remove(main.c_str());
}

TEST(XsdCompileFile, MissingFileIsNullWithStatus) {
    LeptrisStatus st = LEPTRIS_OK;
    EXPECT_EQ(leptris_xsd_compile_file("/no/such/schema.xsd", &st),
              (LeptrisXsdSchema)0);
    EXPECT_NE(st, LEPTRIS_OK);
    EXPECT_EQ(leptris_xsd_compile_file(NULL, &st), (LeptrisXsdSchema)0);
}

}  // namespace
