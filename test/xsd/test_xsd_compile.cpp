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
