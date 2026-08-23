/* TODO.bindings/02 + /04 — pull (StAX-style) and iterparse specs. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <string>
#include <vector>

static const char* kDoc =
    "<library><book n='1'><title>A</title></book>"
    "<book n='4'><title>B</title></book></library>";

TEST(Pull, WalksEveryEventInOrder) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    ASSERT_NE(p, nullptr);

    std::vector<std::string> seq;
    const LeptrisPullEvent* ev;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        switch (ev->type) {
            case LEPTRIS_PULL_START_ELEMENT:
                seq.push_back("<" + std::string(ev->name) + ">");
                break;
            case LEPTRIS_PULL_END_ELEMENT:
                seq.push_back("</" + std::string(ev->name) + ">");
                break;
            case LEPTRIS_PULL_TEXT:
                if (ev->text_len) seq.push_back(ev->text);
                break;
            default: break;
        }
    }
    leptris_pull_free(p);

    ASSERT_EQ(seq.size(), 12u);
    EXPECT_EQ(seq[0], "<library>");
    EXPECT_EQ(seq[1], "<book>");
    EXPECT_EQ(seq[2], "<title>");
    EXPECT_EQ(seq[3], "A");
    EXPECT_EQ(seq[4], "</title>");
    EXPECT_EQ(seq[5], "</book>");
    EXPECT_EQ(seq[10], "</book>");
    EXPECT_EQ(seq[11], "</library>");
}

TEST(Pull, AttributesVisibleDuringStartElement) {
    const char* xml = "<e a='1' b='two'/>";
    LeptrisPullParser p = leptris_pull_new(xml, strlen(xml));
    ASSERT_NE(p, nullptr);
    const LeptrisPullEvent* ev = leptris_pull_next(p);
    ASSERT_NE(ev, nullptr);
    ASSERT_EQ(ev->type, LEPTRIS_PULL_START_ELEMENT);
    EXPECT_EQ(leptris_pull_attr_count(p), 2u);
    EXPECT_STREQ(leptris_pull_attr_name(p, 0), "a");
    EXPECT_STREQ(leptris_pull_attr_value(p, 0), "1");
    EXPECT_STREQ(leptris_pull_attr_name(p, 1), "b");
    EXPECT_STREQ(leptris_pull_attr_value(p, 1), "two");
    EXPECT_EQ(leptris_pull_attr_name(p, 2), nullptr);
    /* Attribute view dies with the next event. */
    ASSERT_NE(leptris_pull_next(p), nullptr);   /* </e> */
    EXPECT_EQ(leptris_pull_attr_count(p), 0u);
    leptris_pull_free(p);
}

TEST(Pull, CommentsCdataPiAndEndDocument) {
    const char* xml = "<r><!--c--><![CDATA[<x>]]><?pi go?></r>";
    LeptrisPullParser p = leptris_pull_new(xml, strlen(xml));
    ASSERT_NE(p, nullptr);
    const LeptrisPullEvent* ev;
    int kind_hits = 0, end_doc = 0;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        switch (ev->type) {
            case LEPTRIS_PULL_COMMENT:
                EXPECT_STREQ(ev->text, "c"); kind_hits++; break;
            case LEPTRIS_PULL_CDATA:
                EXPECT_STREQ(ev->text, "<x>"); kind_hits++; break;
            case LEPTRIS_PULL_PI:
                EXPECT_STREQ(ev->name, "pi");
                EXPECT_STREQ(ev->text, "go"); kind_hits++; break;
            case LEPTRIS_PULL_END_DOCUMENT: end_doc = 1; break;
            default: break;
        }
    }
    leptris_pull_free(p);
    EXPECT_EQ(kind_hits, 3);
    EXPECT_EQ(end_doc, 1);
}

TEST(Pull, MalformedInputYieldsErrorEvent) {
    LeptrisPullParser p = leptris_pull_new("<a><b></a>", 10);
    ASSERT_NE(p, nullptr);
    const LeptrisPullEvent* ev;
    int saw_error = 0;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        if (ev->type == LEPTRIS_PULL_ERROR) {
            saw_error = 1;
            EXPECT_NE(ev->text, nullptr);
            break;
        }
    }
    leptris_pull_free(p);
    EXPECT_EQ(saw_error, 1);
}

TEST(Pull, InvalidInputRejected) {
    EXPECT_EQ(leptris_pull_new(nullptr, 5), nullptr);
    EXPECT_EQ(leptris_pull_new("<a/>", 0), nullptr);
}

TEST(Iterparse, YieldsTopLevelChildren) {
    LeptrisIterparse it = leptris_iterparse_new(kDoc, strlen(kDoc));
    ASSERT_NE(it, nullptr);
    LeptrisElement e;
    int n = 0;
    while ((e = leptris_iterparse_next(it)) != nullptr) {
        EXPECT_STREQ(leptris_element_name(e), "book");
        n++;
    }
    leptris_iterparse_free(it);
    EXPECT_EQ(n, 2);
}

TEST(Iterparse, SubtreeIsQueryableBeforeRelease) {
    LeptrisIterparse it = leptris_iterparse_new(kDoc, strlen(kDoc));
    ASSERT_NE(it, nullptr);
    LeptrisElement e = leptris_iterparse_next(it);
    ASSERT_NE(e, nullptr);
    /* The materialized subtree answers structure queries. */
    LeptrisElement title = leptris_element_first_child_any(e);
    ASSERT_NE(title, nullptr);
    EXPECT_STREQ(leptris_element_name(title), "title");
    EXPECT_EQ(leptris_element_child_count(e), 1u);
    leptris_iterparse_free(it);
}

TEST(Iterparse, InvalidInputRejected) {
    EXPECT_EQ(leptris_iterparse_new(nullptr, 3), nullptr);
    EXPECT_EQ(leptris_iterparse_new("<a/>", 0), nullptr);
}

/* TODO.engine/01 — file-backed sources. */
#include <cstdio>

namespace {
char* write_temp(const char* content) {
    char* path = (char*)malloc(64);
    snprintf(path, 64, "/tmp/leptris_pull_test_%d.xml", getpid());
    FILE* f = fopen(path, "wb");
    if (!f) { free(path); return nullptr; }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return path;
}
}  // namespace

TEST(Pull, FileSourceMatchesMemorySource) {
    char* path = write_temp(
        "<r><a x='1'/>text<a x='2'/><!--c--><?pi d?><![CDATA[cd]]></r>");
    ASSERT_NE(path, nullptr);
    LeptrisPullParser p = leptris_pull_new_file(path);
    ASSERT_NE(p, nullptr);

    int starts = 0, ends = 0, text = 0, comment = 0, cdata = 0, pi = 0;
    int end_doc = 0;
    const LeptrisPullEvent* ev;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        switch (ev->type) {
            case LEPTRIS_PULL_START_ELEMENT: starts++; break;
            case LEPTRIS_PULL_END_ELEMENT: ends++; break;
            case LEPTRIS_PULL_TEXT: text++; break;
            case LEPTRIS_PULL_COMMENT: comment++; break;
            case LEPTRIS_PULL_CDATA: cdata++; break;
            case LEPTRIS_PULL_PI: pi++; break;
            case LEPTRIS_PULL_END_DOCUMENT: end_doc++; break;
            default: break;
        }
    }
    leptris_pull_free(p);
    EXPECT_EQ(starts, 3);
    EXPECT_EQ(ends, 3);
    EXPECT_EQ(text, 1);
    EXPECT_EQ(comment, 1);
    EXPECT_EQ(cdata, 1);
    EXPECT_EQ(pi, 1);
    EXPECT_EQ(end_doc, 1);

    /* Missing file is a clean NULL. */
    EXPECT_EQ(leptris_pull_new_file("/nonexistent/leptris-test.xml"),
              nullptr);
    remove(path);
    free(path);
}

TEST(Iterparse, FileSourceYieldsSubtrees) {
    char* path = write_temp(
        "<library><book n='1'><title>A</title></book>"
        "<book n='4'><title>B</title></book></library>");
    ASSERT_NE(path, nullptr);
    LeptrisIterparse it = leptris_iterparse_new_file(path);
    ASSERT_NE(it, nullptr);
    LeptrisElement e;
    int n = 0;
    while ((e = leptris_iterparse_next(it)) != nullptr) {
        EXPECT_STREQ(leptris_element_name(e), "book");
        EXPECT_EQ(leptris_element_child_count(e), 1u);
        n++;
    }
    leptris_iterparse_free(it);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(leptris_iterparse_new_file("/nonexistent/x.xml"), nullptr);
    remove(path);
    free(path);
}
