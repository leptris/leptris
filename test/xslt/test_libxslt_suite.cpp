/* test/xslt/test_libxslt_suite.cpp — the libxslt general test
 * suite as behavioral specifications for our XSLT 1.0 engine.
 *
 * Files under test/xslt/libxslt_suite_general are copied verbatim
 * from libxslt (MIT; see ATTRIBUTION.md there). Each case is
 * {base}.xml + {base}.xsl + {base}.out. We compile the stylesheet,
 * apply, serialize, and compare against the expected output after
 * a conservative normalization (trailing whitespace, blank lines,
 * XML-declaration line) — libxslt formats declarations and
 * indentation slightly differently by design.
 *
 * Cases that depend on libxslt-specific extensions or behaviors we
 * deliberately do not carry are listed in kSkip with the reason. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <process.h>
#endif
#include <sys/wait.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace {

struct Case {
    std::string base, xml_path, xsl_path, out_path;
};

/* Skip list: libxslt-specific behaviors we do not implement.
 * Each entry names the case base + the reason. */
struct Skip {
    const char* base;
    const char* why;
};
const Skip kSkip[] = {
    /* libxslt registers exsl:document / exsl:node-set style output
     * side-effects and non-spec extension attributes; re-evaluate
     * as our EXSLT surface grows. */
    {nullptr, nullptr},
};

bool is_skipped(const std::string& base) {
    for (const Skip* s = kSkip; s->base; s++)
        if (base == s->base) return true;
    return false;
}

/* Open-worklist cases (test/xslt/open_cases.txt, one base per
 * line): known-failing against the libxslt reference. They SKIP —
 * CI gates on REGRESSIONS (a case outside the list failing) and on
 * the list shrinking as fixes land. */
std::vector<std::string> load_open_cases() {
    std::vector<std::string> v;
    FILE* f = fopen(LEPTRIS_XSLT_OPEN_LIST, "r");
    if (!f) return v;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r' ||
                     line[n-1] == ' '))
            line[--n] = 0;
        if (n) v.push_back(line);
    }
    fclose(f);
    return v;
}

const std::vector<std::string>& open_cases() {
    static std::vector<std::string> v = load_open_cases();
    return v;
}

bool is_open(const std::string& base) {
    for (const std::string& o : open_cases())
        if (o == base) return true;
    return false;
}

/* Collect complete {base}.xml/.xsl/.out triples. Portable: POSIX
 * dirent or Win32 FindFirstFile. */
static void collect_dir(const std::string& dir,
                        std::vector<Case>& cases) {
#ifdef _WIN32
    std::string pattern = dir + "\\*.xsl";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char* name = fd.cFileName;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".xsl") != 0) continue;
        std::string base(name, len - 4);
        std::string xml = dir + "\\" + base + ".xml";
        std::string out = dir + "\\" + base + ".out";
        FILE* f1 = fopen(xml.c_str(), "rb");
        FILE* f2 = fopen(out.c_str(), "rb");
        if (!f1 || !f2) {
            if (f1) fclose(f1);
            if (f2) fclose(f2);
            continue;
        }
        fclose(f1); fclose(f2);
        cases.push_back(Case{base, xml, dir + "\\" + base + ".xsl", out});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".xsl") != 0) continue;
        std::string base(name, len - 4);
        std::string xml = dir + "/" + base + ".xml";
        std::string out = dir + "/" + base + ".out";
        FILE* f1 = fopen(xml.c_str(), "rb");
        FILE* f2 = fopen(out.c_str(), "rb");
        if (!f1 || !f2) {
            if (f1) fclose(f1);
            if (f2) fclose(f2);
            continue;
        }
        fclose(f1); fclose(f2);
        cases.push_back(Case{base, xml, dir + "/" + base + ".xsl", out});
    }
    closedir(d);
#endif
}

std::vector<Case> discover() {
    std::vector<Case> cases;
    collect_dir(LEPTRIS_XSLT_SUITE_DIR, cases);
    /* dirent order is unspecified — sort for deterministic runs. */
    struct Less {
        bool operator()(const Case& a, const Case& b) const {
            return a.base < b.base;
        }
    };
    std::sort(cases.begin(), cases.end(), Less());
    return cases;
}

std::string slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

/* Normalize: strip the XML declaration line, trailing spaces per
 * line, and collapse blank lines. */
std::string normalize(const std::string& in) {
    std::string out;
    size_t i = 0;
    if (in.compare(0, 5, "<?xml") == 0) {
        size_t end = in.find("?>");
        if (end != std::string::npos) i = end + 2;
    }
    bool at_line_start = true;
    int blank_run = 0;
    while (i < in.size()) {
        /* consume one line */
        size_t nl = in.find('\n', i);
        std::string line = in.substr(i,
            nl == std::string::npos ? std::string::npos : nl - i);
        i = (nl == std::string::npos) ? in.size() : nl + 1;
        /* rtrim */
        while (!line.empty() &&
               (line.back() == ' ' || line.back() == '\r' ||
                line.back() == '\t'))
            line.pop_back();
        if (line.empty()) {
            blank_run++;
            continue;
        }
        if (!out.empty() && !at_line_start) out += '\n';
        while (blank_run > 0 && !out.empty()) {
            /* libxslt blank-line conventions differ; collapse */
            blank_run--;
        }
        out += line;
        at_line_start = false;
        blank_run = 0;
    }
    return out;
}

class LibxsltSuite : public ::testing::TestWithParam<Case> {};

/* Fork-isolated case run: cross-case engine state (the process-wide
 * AST cache) can turn one case's corruption into a later crash, and
 * a crashing case must fail, not stop the suite. The child writes
 * its serialized result + a status byte to a pipe. */
std::string run_case(const Case& c, int* status) {
    int fds[2];
    if (pipe(fds) != 0) { *status = -1; return ""; }
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        /* xsl:import/include and document() hrefs resolve relative
         * to the stylesheet's location — run from the suite dir. */
#ifdef _WIN32
        _chdir(LEPTRIS_XSLT_SUITE_DIR);
#else
        chdir(LEPTRIS_XSLT_SUITE_DIR);
#endif
        std::string xsl = slurp(c.xsl_path.c_str());
        std::string xml = slurp(c.xml_path.c_str());
        LeptrisXslt sheet = leptris_xslt_parse(xsl.c_str(), xsl.size());
        if (!sheet) {
            dprintf(fds[1], "Ecompile");
            _exit(2);
        }
        LeptrisDocument doc = leptris_parse_string(xml.c_str(),
                                                   xml.size(), nullptr);
        if (!doc) {
            leptris_xslt_free(sheet);
            dprintf(fds[1], "Eparse");
            _exit(3);
        }
        char* got = leptris_xslt_apply_string(sheet, doc);
        leptris_document_free(doc);
        leptris_xslt_free(sheet);
        if (!got) {
            dprintf(fds[1], "Eapply");
            _exit(4);
        }
        dprintf(fds[1], "O%s", got);
        leptris_free_string(got);
        _exit(0);
    }
    close(fds[1]);
    std::string out;
    char buf[8192];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) out.append(buf, n);
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    *status = st;
    return out;
}

TEST_P(LibxsltSuite, MatchesLibxsltOutput) {
    const Case& c = GetParam();
    if (is_skipped(c.base)) GTEST_SKIP();
    if (is_open(c.base))
        GTEST_SKIP() << "open worklist case " << c.base
                     << " (test/xslt/open_cases.txt)";

    std::string want = slurp(c.out_path.c_str());
    int status = 0;
    std::string result = run_case(c, &status);

    ASSERT_FALSE(result.rfind("Ecompile", 0) == 0)
        << "case " << c.base << ": stylesheet failed to compile";
    ASSERT_FALSE(result.rfind("Eparse", 0) == 0)
        << "case " << c.base << ": source failed to parse";
    ASSERT_FALSE(result.rfind("Eapply", 0) == 0)
        << "case " << c.base << ": transform produced nothing";
    ASSERT_EQ(status, 0) << "case " << c.base
                         << ": runner exited abnormally (crash?)";

    std::string actual = result.empty() ? "" : result.substr(1);
    EXPECT_EQ(normalize(actual), normalize(want)) << "case " << c.base;
}

INSTANTIATE_TEST_SUITE_P(General, LibxsltSuite,
                         ::testing::ValuesIn(discover()));

}  // namespace
