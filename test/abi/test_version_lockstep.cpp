// test/abi/test_version_lockstep.cpp — The four version surfaces agree.
//
// The engine's version lives in four hand-reachable places:
//   1. CMakeLists.txt              project(leptris VERSION x.y.z)
//   2. src/include/leptris.h       LIBLEPTRIS_VERSION_{MAJOR,MINOR,PATCH,STRING}
//   3. src/leptris/core.c          the non-CMake fallback block
//   4. the compiled library        leptris_version()
//
// Sources 1/2/4 are what release trains and bindings consume; 3 is
// what hand-built compilers fall back to. The release train's bump
// script updates 1+2+3, and the release workflow commits them —
// until the commit step dropped leptris.h (287 releases shipped with
// the header at 1.9.145, npm 0.1.66/0.1.67 shipped wasm reporting
// 1.9.390). This spec fails loudly the moment any surface drifts.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

#include "leptris.h"

#ifndef LEPTRIS_REPO_ROOT
#define LEPTRIS_REPO_ROOT "."
#endif

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::string cmake_version() {
    const std::string text = read_file(std::string(LEPTRIS_REPO_ROOT) + "/CMakeLists.txt");
    std::smatch m;
    const std::regex re("VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)");
    if (!std::regex_search(text, m, re)) return "";
    return m[1].str() + "." + m[2].str() + "." + m[3].str();
}

std::string header_version() {
    const std::string text =
        read_file(std::string(LEPTRIS_REPO_ROOT) + "/src/include/leptris.h");
    std::smatch m;
    const std::regex re("LIBLEPTRIS_VERSION_STRING \"([^\"]+)\"");
    if (!std::regex_search(text, m, re)) return "";
    return m[1].str();
}

std::string core_fallback_version() {
    const std::string text =
        read_file(std::string(LEPTRIS_REPO_ROOT) + "/src/leptris/core.c");
    std::smatch m;
    const std::regex re("#define LEPTRIS_VERSION \"([^\"]+)\"");
    if (!std::regex_search(text, m, re)) return "";
    return m[1].str();
}

TEST(VersionLockstep, AllFourSurfacesAgree) {
    const std::string cmake = cmake_version();
    const std::string header = header_version();
    const std::string fallback = core_fallback_version();
    const std::string runtime = leptris_version();

    ASSERT_FALSE(cmake.empty()) << "CMakeLists.txt lost project(... VERSION x.y.z)";
    ASSERT_FALSE(header.empty()) << "leptris.h lost LIBLEPTRIS_VERSION_STRING";
    ASSERT_FALSE(fallback.empty()) << "core.c lost the LEPTRIS_VERSION fallback";
    ASSERT_FALSE(runtime.empty()) << "leptris_version() returned empty";

    EXPECT_EQ(header, cmake)
        << "leptris.h macros (" << header << ") drifted from CMakeLists.txt (" << cmake
        << ") — the release train's commit step must carry src/include/leptris.h";
    EXPECT_EQ(fallback, cmake)
        << "core.c's non-CMake fallback (" << fallback << ") drifted from CMakeLists.txt ("
        << cmake << ") — bump-version.sh must sync the fallback block";
    EXPECT_EQ(runtime, cmake)
        << "the compiled leptris_version() (" << runtime << ") disagrees with CMakeLists.txt ("
        << cmake << ") — the build injected a different version than the project declares";
}

}  // namespace
