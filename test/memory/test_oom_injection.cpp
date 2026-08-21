// test/memory/test_oom_injection.cpp — allocation-failure injection.
//
// Parses, serializes and mutates under an allocator that fails at a
// chosen allocation index: every path must return an error (or a
// NULL document), never crash, never read freed memory, and never
// hang. Sweeps the first N allocation sites of each operation so a
// NULL-check forgotten at ANY early allocation turns into a test
// failure instead of a user's segfault.
//
// The hook (taurus_set_memory_management_functions) covers the pool/
// arena-backed allocations the parser and DOM use. Paths that raw-
// malloc (serialize's output buffer, mutation bump blocks) are out
// of scope here — they are covered by the large-document suite.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstdlib>
#include <cstring>

namespace {

/* Countdown allocator: allocation number `fail_at` (1-based)
 * returns NULL; everything else delegates to malloc. */
long g_alloc_countdown = -1;   /* -1 = never fail */

void* countdown_alloc(size_t size) {
    if (g_alloc_countdown == 0) return NULL;
    if (g_alloc_countdown > 0) g_alloc_countdown--;
    return std::malloc(size);
}

void passthrough_free(void* p) { std::free(p); }

class OomInjection : public ::testing::Test {
protected:
    void SetUp() override {
        taurus_set_memory_management_functions(countdown_alloc,
                                                passthrough_free);
    }
    void TearDown() override {
        g_alloc_countdown = -1;
        taurus_set_memory_management_functions(NULL, NULL);
    }
};

const char kDoc[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<users>\n"
    "<user id=\"1\"><name>User 1</name><email>u1@example.com</email>"
    "<created>2023-01-01T10:00:00Z</created></user>\n"
    "<user id=\"2\"><name>User 2</name></user>\n"
    "</users>\n";

}  // namespace

// Parse under a failure at allocation #k for k = 1..cap. Any
// outcome except a crash is acceptable: NULL document (clean
// failure) or a valid document (the failure landed in an optional
// allocation) which must then free cleanly.
TEST_F(OomInjection, ParseSurvivesFailureAtEveryEarlyAllocation) {
    const int cap = 512;
    for (int k = 1; k <= cap; k++) {
        SCOPED_TRACE(std::string("parse fail at alloc #") +
                     std::to_string(k));
        g_alloc_countdown = k;
        TaurusStatus st = (TaurusStatus)0;
        TaurusDocument doc =
            taurus_parse_string(kDoc, std::strlen(kDoc), &st);
        if (doc) {
            /* Valid doc: must be usable and freeable. */
            TaurusElement root = taurus_document_root(doc);
            EXPECT_NE(root, nullptr);
            taurus_document_free(doc);
        } else {
            EXPECT_NE(st, (TaurusStatus)0) << "NULL doc needs a status";
        }
        g_alloc_countdown = -1;
    }
}

// Serialize under injected failures at every early allocation.
// The document itself is parsed with a healthy allocator; the
// countdown applies during serialize only.
TEST_F(OomInjection, SerializeSurvivesFailureAtEveryEarlyAllocation) {
    g_alloc_countdown = -1;
    TaurusStatus st = (TaurusStatus)0;
    TaurusDocument doc = taurus_parse_string(kDoc, std::strlen(kDoc), &st);
    ASSERT_NE(doc, nullptr);

    const int cap = 256;
    for (int k = 1; k <= cap; k++) {
        SCOPED_TRACE(std::string("serialize fail at alloc #") +
                     std::to_string(k));
        g_alloc_countdown = k;
        char* out = taurus_document_serialize(doc, NULL);
        if (out) taurus_free_string(out);
        g_alloc_countdown = -1;
        /* Document must survive every serialize failure intact. */
    }

    /* After all the failures, a clean serialize must still work
     * (no corrupted internal state). */
    char* ok = taurus_document_serialize(doc, NULL);
    ASSERT_NE(ok, nullptr);
    EXPECT_NE(std::strstr(ok, "User 1"), nullptr);
    taurus_free_string(ok);
    taurus_document_free(doc);
}

// Mutation under injected failures: create elements, set
// attributes, append — every call must return a status, never
// crash, and the tree must stay walkable afterwards.
TEST_F(OomInjection, MutationSurvivesFailureAtEveryEarlyAllocation) {
    const int cap = 256;
    for (int k = 1; k <= cap; k++) {
        SCOPED_TRACE(std::string("mutation fail at alloc #") +
                     std::to_string(k));
        g_alloc_countdown = k;
        TaurusStatus st = (TaurusStatus)0;
        TaurusDocument doc = taurus_parse_string(kDoc, std::strlen(kDoc), &st);
        if (!doc) { g_alloc_countdown = -1; continue; }
        TaurusElement root = taurus_document_root(doc);

        for (int i = 0; i < 20; i++) {
            TaurusElement c = taurus_element_create(doc, "n");
            if (!c) break; /* allocation failed: allowed */
            taurus_element_set_attribute(c, "k", "v");
            taurus_element_append_child(root, c);
        }
        g_alloc_countdown = -1;

        /* The tree must still be consistent: walk it and free. */
        char* out = taurus_document_serialize(doc, NULL);
        if (out) taurus_free_string(out);
        taurus_document_free(doc);
    }
}

// Free must be robust to nothing — but run it under the hook with
// failures exhausted so no allocation during teardown can crash.
TEST_F(OomInjection, FreeNeverCrashesUnderHook) {
    g_alloc_countdown = -1;
    TaurusStatus st = (TaurusStatus)0;
    TaurusDocument doc = taurus_parse_string(kDoc, std::strlen(kDoc), &st);
    ASSERT_NE(doc, nullptr);
    /* Make the allocator refuse everything during teardown. */
    g_alloc_countdown = 0;
    taurus_document_free(doc);
    g_alloc_countdown = -1;
}

// The failure must actually fire: with fail_at = 1 a parse of a
// non-trivial document cannot succeed (the very first pool page
// allocation fails). Guards against the hook silently not applying,
// which would make every sweep above vacuous.
TEST_F(OomInjection, HookIsActuallyWired) {
    g_alloc_countdown = 1;
    TaurusStatus st = (TaurusStatus)0;
    TaurusDocument doc =
        taurus_parse_string(kDoc, std::strlen(kDoc), &st);
    /* Either it fails cleanly (expected) or allocations bypass the
     * hook (vacuous suite) — a valid doc here means the latter. */
    if (doc) {
        taurus_document_free(doc);
        FAIL() << "allocation hook does not cover the parse path — "
                  "the OOM sweeps above are vacuous";
    }
}
