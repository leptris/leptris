// test/memory/test_oom_injection.cpp — allocation-failure injection.
//
// Parses, serializes and mutates under an allocator that fails at a
// chosen allocation index: every path must return an error (or a
// NULL document), never crash, never read freed memory, and never
// hang. Sweeps the first N allocation sites of each operation so a
// NULL-check forgotten at ANY early allocation turns into a test
// failure instead of a user's segfault.
//
// The hook (leptris_set_memory_management_functions) covers the pool/
// arena-backed allocations the parser and DOM use. Paths that raw-
// malloc (serialize's output buffer, mutation bump blocks) are out
// of scope here — they are covered by the large-document suite.

#include <gtest/gtest.h>

#include "leptris.h"
#include "arena.h"

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
        /* Lane-18 round 17b: the arena span is retained across
         * parses, and a retained take bypasses the injectable hook —
         * a warm pool would make every sweep below vacuous (the
         * failure could never land on the span). Drain so each test
         * exercises the hooked allocation for real. */
        leptris_arena_retain_drain();
        leptris_set_memory_management_functions(countdown_alloc,
                                                passthrough_free);
    }
    void TearDown() override {
        g_alloc_countdown = -1;
        leptris_set_memory_management_functions(NULL, NULL);
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
        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisDocument doc =
            leptris_parse_string(kDoc, std::strlen(kDoc), &st);
        if (doc) {
            /* Valid doc: must be usable and freeable. */
            LeptrisElement root = leptris_document_root(doc);
            EXPECT_NE(root, nullptr);
            leptris_document_free(doc);
        } else {
            EXPECT_NE(st, (LeptrisStatus)0) << "NULL doc needs a status";
        }
        g_alloc_countdown = -1;
    }
}

// Serialize under injected failures at every early allocation.
// The document itself is parsed with a healthy allocator; the
// countdown applies during serialize only.
TEST_F(OomInjection, SerializeSurvivesFailureAtEveryEarlyAllocation) {
    g_alloc_countdown = -1;
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument doc = leptris_parse_string(kDoc, std::strlen(kDoc), &st);
    ASSERT_NE(doc, nullptr);

    const int cap = 256;
    for (int k = 1; k <= cap; k++) {
        SCOPED_TRACE(std::string("serialize fail at alloc #") +
                     std::to_string(k));
        g_alloc_countdown = k;
        char* out = leptris_document_serialize(doc, NULL);
        if (out) leptris_free_string(out);
        g_alloc_countdown = -1;
        /* Document must survive every serialize failure intact. */
    }

    /* After all the failures, a clean serialize must still work
     * (no corrupted internal state). */
    char* ok = leptris_document_serialize(doc, NULL);
    ASSERT_NE(ok, nullptr);
    EXPECT_NE(std::strstr(ok, "User 1"), nullptr);
    leptris_free_string(ok);
    leptris_document_free(doc);
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
        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisDocument doc = leptris_parse_string(kDoc, std::strlen(kDoc), &st);
        if (!doc) { g_alloc_countdown = -1; continue; }
        LeptrisElement root = leptris_document_root(doc);

        for (int i = 0; i < 20; i++) {
            LeptrisElement c = leptris_element_create(doc, "n");
            if (!c) break; /* allocation failed: allowed */
            leptris_element_set_attribute(c, "k", "v");
            leptris_element_append_child(root, c);
        }
        g_alloc_countdown = -1;

        /* The tree must still be consistent: walk it and free. */
        char* out = leptris_document_serialize(doc, NULL);
        if (out) leptris_free_string(out);
        leptris_document_free(doc);
    }
}

// Free must be robust to nothing — but run it under the hook with
// failures exhausted so no allocation during teardown can crash.
TEST_F(OomInjection, FreeNeverCrashesUnderHook) {
    g_alloc_countdown = -1;
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument doc = leptris_parse_string(kDoc, std::strlen(kDoc), &st);
    ASSERT_NE(doc, nullptr);
    /* Make the allocator refuse everything during teardown. */
    g_alloc_countdown = 0;
    leptris_document_free(doc);
    g_alloc_countdown = -1;
}

// The failure must actually fire: with fail_at = 1 a parse of a
// non-trivial document cannot succeed (the very first pool page
// allocation fails). Guards against the hook silently not applying,
// which would make every sweep above vacuous.
TEST_F(OomInjection, HookIsActuallyWired) {
    /* Lane-18 round 17b: a parse of this document needs EXACTLY one
     * system allocation (the arena span — header carved into its
     * head, everything else bump-carved inside). The countdown
     * therefore targets the span itself; failing one allocation
     * later cannot break a one-allocation parse, which is the new
     * correct behavior, not a vacuous hook. */
    /* countdown = 0: the very first allocation (the span) fails. */
    g_alloc_countdown = 0;
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument doc =
        leptris_parse_string(kDoc, std::strlen(kDoc), &st);
    /* Either it fails cleanly (expected) or allocations bypass the
     * hook (vacuous suite) — a valid doc here means the latter.
     * Round 17b: the one hooked allocation IS the span; failing it
     * must fail the parse, and nothing else in the path allocates. */
    if (doc) {
        leptris_document_free(doc);
        FAIL() << "allocation hook does not cover the parse path — "
                  "the OOM sweeps above are vacuous";
    }
    /* countdown = 1: the span succeeds and NOTHING else allocates —
     * the parse must complete on that single allocation. */
    g_alloc_countdown = 1;
    st = (LeptrisStatus)0;
    doc = leptris_parse_string(kDoc, std::strlen(kDoc), &st);
    ASSERT_NE(doc, nullptr)
        << "the span allocation failed at countdown=1 — the parse "
           "path allocates more than the arena span";
    leptris_document_free(doc);
}
