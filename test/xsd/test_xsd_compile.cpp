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
        std::string text = slurp(path.c_str());
        ASSERT_FALSE(text.empty()) << path;
        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisXsdSchema s =
            leptris_xsd_compile(text.data(), text.size(), &st);
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
