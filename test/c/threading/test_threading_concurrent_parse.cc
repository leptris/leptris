/* test_threading_concurrent_parse.c - Threading tests for per-document concurrency
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests that multiple documents can be parsed concurrently with different
 * strict mode settings without interference.
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include <cstring>
#include <string>
#include "../../src/include/taurus.h"

namespace taurus_test {

/* Test data for concurrent parsing */
struct ParseThreadData {
    const char* xml;
    size_t xml_len;
    int strict_mode;
    int expected_status;  /* TAURUS_OK or TAURUS_ERROR_PARSE */
    int parse_result;     /* Result: 1 = success, 0 = failure, -1 = not set */
    char error_msg[256];
};

/* Thread function for concurrent parsing */
static void* parse_thread_func(void* arg) {
    ParseThreadData* data = static_cast<ParseThreadData*>(arg);

    /* Set global strict mode for this thread's document creation */
    taurus_set_strict_mode(data->strict_mode);

    /* Parse the XML */
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(data->xml, data->xml_len, &status);

    if (data->expected_status == TAURUS_OK) {
        /* Should succeed */
        if (doc && status == TAURUS_OK) {
            data->parse_result = 1;  /* Success */
        } else {
            data->parse_result = 0;  /* Unexpected failure */
            snprintf(data->error_msg, sizeof(data->error_msg),
                     "Expected success but got status %d", status);
        }
    } else {
        /* Should fail */
        if (!doc || status != TAURUS_OK) {
            data->parse_result = 1;  /* Expected failure */
        } else {
            data->parse_result = 0;  /* Unexpected success */
            snprintf(data->error_msg, sizeof(data->error_msg),
                     "Expected failure but parsing succeeded");
        }
    }

    if (doc) {
        taurus_document_free(doc);
    }

    return nullptr;
}

/* ============================================================================
 * Concurrent Parsing Tests
 * ============================================================================ */

class ThreadingConcurrentParseTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Reset to lenient mode before each test */
        taurus_set_strict_mode(0);
    }

    void TearDown() override {
        /* Reset to lenient mode after each test */
        taurus_set_strict_mode(0);
    }
};

/* Test: Parse multiple valid documents concurrently */
TEST_F(ThreadingConcurrentParseTest, ConcurrentValidDocuments) {
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    ParseThreadData thread_data[NUM_THREADS];

    const char* valid_xml = "<root>content</root>";

    /* Initialize thread data */
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].xml = valid_xml;
        thread_data[i].xml_len = strlen(valid_xml);
        thread_data[i].strict_mode = 0;  /* Lenient */
        thread_data[i].expected_status = TAURUS_OK;
        thread_data[i].parse_result = -1;
        thread_data[i].error_msg[0] = '\0';
    }

    /* Create threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], nullptr, parse_thread_func, &thread_data[i]);
        ASSERT_EQ(rc, 0) << "Failed to create thread " << i;
    }

    /* Wait for threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    /* Verify all threads succeeded */
    for (int i = 0; i < NUM_THREADS; i++) {
        EXPECT_EQ(thread_data[i].parse_result, 1)
            << "Thread " << i << " failed: " << thread_data[i].error_msg;
    }
}

/* Test: Parse with mixed strict/lenient modes concurrently */
TEST_F(ThreadingConcurrentParseTest, ConcurrentMixedModes) {
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    ParseThreadData thread_data[NUM_THREADS];

    /* Valid XML for lenient mode - invalid entity should fail in strict */
    const char* invalid_entity_xml = "<root>&invalid;</root>";
    const char* valid_xml = "<root>content</root>";

    /* Thread 0, 2: Lenient mode - should succeed with invalid entity (copied literally) */
    thread_data[0].xml = invalid_entity_xml;
    thread_data[0].xml_len = strlen(invalid_entity_xml);
    thread_data[0].strict_mode = 0;
    thread_data[0].expected_status = TAURUS_OK;

    /* Thread 1, 3: Strict mode - should fail with invalid entity */
    thread_data[1].xml = invalid_entity_xml;
    thread_data[1].xml_len = strlen(invalid_entity_xml);
    thread_data[1].strict_mode = 1;
    thread_data[1].expected_status = TAURUS_ERROR_PARSE;

    thread_data[2].xml = valid_xml;
    thread_data[2].xml_len = strlen(valid_xml);
    thread_data[2].strict_mode = 0;
    thread_data[2].expected_status = TAURUS_OK;

    thread_data[3].xml = valid_xml;
    thread_data[3].xml_len = strlen(valid_xml);
    thread_data[3].strict_mode = 1;
    thread_data[3].expected_status = TAURUS_OK;

    /* Initialize remaining fields */
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].parse_result = -1;
        thread_data[i].error_msg[0] = '\0';
    }

    /* Create threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], nullptr, parse_thread_func, &thread_data[i]);
        ASSERT_EQ(rc, 0) << "Failed to create thread " << i;
    }

    /* Wait for threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    /* Verify results */
    for (int i = 0; i < NUM_THREADS; i++) {
        EXPECT_EQ(thread_data[i].parse_result, 1)
            << "Thread " << i << " failed: " << thread_data[i].error_msg;
    }
}

/* Test: Verify per-document strict mode is independent */
TEST_F(ThreadingConcurrentParseTest, PerDocumentStrictMode) {
    /* Create document in strict mode */
    taurus_set_strict_mode(1);
    TaurusStatus status1;
    const char* valid_xml = "<root>content</root>";
    TaurusDocument doc1 = taurus_parse_string(valid_xml, strlen(valid_xml), &status1);
    ASSERT_NE(doc1, nullptr);

    /* Verify doc1 has strict mode */
    EXPECT_EQ(taurus_document_get_strict(doc1), 1);

    /* Change global to lenient */
    taurus_set_strict_mode(0);

    /* Create second document */
    TaurusStatus status2;
    TaurusDocument doc2 = taurus_parse_string(valid_xml, strlen(valid_xml), &status2);
    ASSERT_NE(doc2, nullptr);

    /* Verify doc2 has lenient mode */
    EXPECT_EQ(taurus_document_get_strict(doc2), 0);

    /* Verify doc1 still has strict mode (independent) */
    EXPECT_EQ(taurus_document_get_strict(doc1), 1);

    /* Change doc2 to strict */
    taurus_document_set_strict(doc2, 1);
    EXPECT_EQ(taurus_document_get_strict(doc2), 1);

    /* Verify doc1 is still strict (unchanged) */
    EXPECT_EQ(taurus_document_get_strict(doc1), 1);

    /* Cleanup */
    taurus_document_free(doc1);
    taurus_document_free(doc2);
}

/* Test: No global state pollution between documents */
TEST_F(ThreadingConcurrentParseTest, NoGlobalStatePollution) {
    /* This test verifies that parsing one document doesn't affect another */
    const char* invalid_entity = "<root>&unknown;</root>";
    const char* valid_xml = "<root>content</root>";

    /* Parse with invalid entity in lenient mode - should succeed */
    taurus_set_strict_mode(0);
    TaurusStatus status1;
    TaurusDocument doc1 = taurus_parse_string(invalid_entity, strlen(invalid_entity), &status1);
    EXPECT_NE(doc1, nullptr) << "Lenient parse should succeed with unknown entity";

    /* Parse valid XML in strict mode - should succeed */
    taurus_set_strict_mode(1);
    TaurusStatus status2;
    TaurusDocument doc2 = taurus_parse_string(valid_xml, strlen(valid_xml), &status2);
    EXPECT_NE(doc2, nullptr) << "Strict parse should succeed with valid XML";

    /* Parse invalid entity in strict mode - should fail */
    TaurusStatus status3;
    TaurusDocument doc3 = taurus_parse_string(invalid_entity, strlen(invalid_entity), &status3);
    EXPECT_EQ(doc3, nullptr) << "Strict parse should fail with unknown entity";

    /* Cleanup */
    if (doc1) taurus_document_free(doc1);
    if (doc2) taurus_document_free(doc2);
    /* doc3 is null, no need to free */
}

} /* namespace taurus_test */

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
