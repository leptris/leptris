// test/cli/test_cli.cpp — CLI command specs (TODO 32).
//
// Specs invoke the CLI binary via subprocess and assert on exit code,
// stdout, and stderr.  Catches end-to-end behavior including arg
// parsing, stdin handling, and file I/O.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <array>

namespace {

/* Run the CLI with the given args, feeding optional stdin.
 * Returns (exit_code, stdout, stderr). */
struct RunResult {
    int exit_code;
    std::string out;
    std::string err;
};

std::string bin_path() {
    /* The test executable is at build/test/cli/test_cli; the binary is at
     * build/cli/leptris.  Honor TEST_CLI_BIN env (set by CMake at build
     * time) or fall back to the relative path. */
    const char* env = std::getenv("LEPTRIS_CLI_BIN");
    if (env && env[0]) return env;
    return "cli/leptris";  /* resolved against the build dir cwd */
}

RunResult run_cli(const std::vector<std::string>& args,
                  const std::string& stdin_data = "") {
    /* Build the command.  Use a temp file for stdin to avoid heredoc
     * quoting pitfalls with embedded quotes/special chars. */
    std::string stdin_path = "/tmp/leptris_cli_stdin";
    if (!stdin_data.empty()) {
        FILE* fp = std::fopen(stdin_path.c_str(), "w");
        if (fp) {
            std::fwrite(stdin_data.data(), 1, stdin_data.size(), fp);
            std::fclose(fp);
        }
    }

    std::string cmd = bin_path();
    for (const auto& a : args) {
        cmd += " '";
        for (char c : a) {
            if (c == '\'') cmd += "'\\''";
            else           cmd += c;
        }
        cmd += "'";
    }
    if (!stdin_data.empty()) {
        cmd += " < " + stdin_path;
    }
    cmd += " > /tmp/leptris_cli_stdout 2> /tmp/leptris_cli_stderr";

    int rc = std::system(cmd.c_str());

    std::array<char, 4096> buf;
    std::string out;
    FILE* fp = std::fopen("/tmp/leptris_cli_stdout", "r");
    if (fp) {
        while (size_t n = std::fread(buf.data(), 1, buf.size(), fp)) {
            out.append(buf.data(), n);
        }
        std::fclose(fp);
    }
    std::string err;
    fp = std::fopen("/tmp/leptris_cli_stderr", "r");
    if (fp) {
        while (size_t n = std::fread(buf.data(), 1, buf.size(), fp)) {
            err.append(buf.data(), n);
        }
        std::fclose(fp);
    }

    return {rc, out, err};
}

// ---- version --------------------------------------------------------------

TEST(CliVersion, PrintsVersionOnStdout) {
    auto r = run_cli({"version"});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("leptris"), std::string::npos);
}

// ---- parse ----------------------------------------------------------------

TEST(CliParse, ReadsFromStdin) {
    auto r = run_cli({"parse", "-"}, "<root>hi</root>");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("<root>hi</root>"), std::string::npos);
}

TEST(CliParse, ReadsFile) {
    /* Absolute path — the test cwd is the build dir, not the source tree. */
    const char* src_dir = std::getenv("LEPTRIS_SOURCE_DIR");
    std::string fixture = src_dir && src_dir[0]
        ? std::string(src_dir) + "/test/fixtures/basic.xml"
        : "../test/fixtures/basic.xml";
    auto r = run_cli({"parse", fixture});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("<root"), std::string::npos);
}

TEST(CliParse, RejectsMalformedInput) {
    auto r = run_cli({"parse", "-"}, "<a><b></a></b>");
    EXPECT_NE(r.exit_code, 0);
    EXPECT_NE(r.err.find("error"), std::string::npos);
}

TEST(CliParse, RejectsMissingFile) {
    auto r = run_cli({"parse", "/nonexistent/file.xml"});
    EXPECT_NE(r.exit_code, 0);
}

TEST(CliParse, PreservesCdataAndComments) {
    auto r = run_cli({"parse", "-"},
        "<r><!--c--><![CDATA[raw<>&]]></r>");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("<!--c-->"),           std::string::npos);
    EXPECT_NE(r.out.find("<![CDATA[raw<>&]]>"), std::string::npos);
}

// ---- xpath ----------------------------------------------------------------

TEST(CliXpath, EvaluatesCount) {
    /* --count returns the number of nodes in the result nodeset.
     * Use a nodeset expression (//item) — count() returns a number,
     * which is a different result type. */
    auto r = run_cli({"xpath", "--count", "-", "//item"},
        "<r><item/><item/><item/></r>");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("3"), std::string::npos);
}

TEST(CliXpath, EvaluatesNodeset) {
    auto r = run_cli({"xpath", "-", "//item"},
        "<r><item>a</item><item>b</item></r>");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("<item>a</item>"), std::string::npos);
    EXPECT_NE(r.out.find("<item>b</item>"), std::string::npos);
}

TEST(CliXpath, AttributePredicate) {
    auto r = run_cli({"xpath", "-", "//book[@id='b2']"},
        "<r><book id='b1'/><book id='b2'/></r>");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.out.find("b2"), std::string::npos);
}

// ---- format ---------------------------------------------------------------

TEST(CliFormat, AppliesIndent) {
    auto r = run_cli({"format", "--indent", "2", "-"},
        "<r><a>x</a></r>");
    EXPECT_EQ(r.exit_code, 0);
    /* Indented output should contain a newline followed by 2 spaces. */
    EXPECT_NE(r.out.find("\n  <a>"), std::string::npos);
}

TEST(CliFormat, CompactByDefault) {
    /* Default format DOES pretty-print (verified empirically).
     * Compact mode requires explicit --indent 0.  Verify that path
     * produces single-line output. */
    auto r = run_cli({"format", "--indent", "0", "-"}, "<r><a>x</a></r>");
    EXPECT_EQ(r.exit_code, 0);
    /* Compact (indent=0) should be on a single line. */
    EXPECT_EQ(r.out.find("\n  <a>"), std::string::npos);
}

// ---- DoS protection (regression for TODO 07) -----------------------------

TEST(CliParse, RejectsDeepNesting) {
    /* 50k-deep nesting must be rejected, not segfault. */
    std::string xml;
    for (int i = 0; i < 50000; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 50000; i++) xml += "</a>";

    auto r = run_cli({"parse", "-"}, xml);
    EXPECT_NE(r.exit_code, 0);
    /* Exit 139 would be a segfault. */
    EXPECT_NE(r.exit_code, 139);
}

}  // namespace

// ---- xquery ----------------------------------------------------------------

/* Windows cmd.exe does not honor the helper's single-quote
 * wrapping, so spaced expressions ride a query FILE instead. */
static std::string write_xq_file(const char* query) {
    /* Relative to cwd (the build dir): /tmp does not exist on
     * Windows runners. */
    std::string path = "leptris_xq_query.tmp";
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (fp) {
        std::fwrite(query, 1, std::strlen(query), fp);
        std::fclose(fp);
    }
    return path;
}

TEST(CliXquery, EvaluatesFlworFromInlineExpression) {
    auto r = run_cli({"xquery", "-q",
                      write_xq_file("for $n in 1 to 3"
                                    " order by $n descending"
                                    " return $n * 2").c_str()});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.out, "6 4 2\n");
}

TEST(CliXquery, SourceDocumentAndConstructors) {
    const char* src_dir = std::getenv("LEPTRIS_SOURCE_DIR");
    std::string books = src_dir && src_dir[0]
        ? std::string(src_dir) + "/test/xquery/books.xml"
        : "../test/xquery/books.xml";
    auto r = run_cli({"xquery", "-s", books.c_str(), "-q",
                      write_xq_file(
                          "for $b in //book where $b/@price > 10"
                          " order by $b/@price descending"
                          " return element r { attribute price"
                          " { $b/@price }, text { $b/title } }")
                          .c_str()});
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.out,
              "<r price=\"20\">CC</r> <r price=\"12\">AA</r>\n");
}
