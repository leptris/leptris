/* xpath_test_utils.h - XPath W3C test utilities
 * Copyright (c) 2024, Ribose Inc.
 *
 * Utilities for W3C XPath 1.0 conformance testing
 */

#ifndef XPATH_TEST_UTILS_H
#define XPATH_TEST_UTILS_H

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "../../../../src/include/taurus.h"

namespace taurus_test {

/**
 * Helper to check if a TaurusElement is null
 */
static inline bool element_is_null(TaurusElement elem) {
    return taurus_element_is_null(elem);
}

/**
 * Helper macros for TaurusElement assertions
 */
#define ASSERT_ELEM_NOT_NULL(elem) ASSERT_TRUE(!taurus_element_is_null(elem))

/**
 * Base class for XPath tests with common setup/teardown
 */
class XPathTestBase : public ::testing::Test {
protected:
    TaurusDocument doc;
    TaurusElement root;

    void SetUp() override {
        doc = nullptr;
        root = taurus_element_handle_null();
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        root = taurus_element_handle_null();  /* Clear root pointer to prevent use-after-free */
    }

    /**
     * Load XML fixture from file
     */
    void load_fixture(const std::string& filepath) {
#ifdef FIXTURES_DIR
        std::string full_path = std::string(FIXTURES_DIR) + "/" + filepath;
#else
        std::string full_path = filepath;
#endif
        std::ifstream file(full_path);
        ASSERT_TRUE(file.is_open()) << "Failed to open fixture: " << full_path;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string xml_content = buffer.str();

        TaurusStatus status;
        doc = taurus_parse_string(xml_content.c_str(), xml_content.length(), &status);
        ASSERT_NE(doc, nullptr) << "Failed to parse XML fixture: " << filepath;
        ASSERT_EQ(status, TAURUS_OK) << "Parse status not OK";

        root = taurus_document_root(doc);
        ASSERT_ELEM_NOT_NULL(root) << "Failed to get root element";
    }

    /**
     * Parse XML string directly
     */
    void parse_xml(const std::string& xml) {
        TaurusStatus status;
        doc = taurus_parse_string(xml.c_str(), xml.length(), &status);
        ASSERT_NE(doc, nullptr) << "Failed to parse XML";
        ASSERT_EQ(status, TAURUS_OK) << "Parse status not OK";

        root = taurus_document_root(doc);
        ASSERT_ELEM_NOT_NULL(root) << "Failed to get root element";
    }

    /**
     * Evaluate XPath expression with document root as context
     */
    TaurusXPathResult eval_xpath(const std::string& expr) {
        return taurus_xpath_eval(doc, root, expr.c_str());
    }

    /**
     * Evaluate XPath expression with custom context element
     */
    TaurusXPathResult eval_xpath_ctx(const std::string& expr, TaurusElement context) {
        return taurus_xpath_eval(doc, context, expr.c_str());
    }
};

/**
 * Assertion macros for XPath results
 */

// Assert result type
#define EXPECT_XPATH_TYPE(result, expected_type) \
    EXPECT_EQ(taurus_xpath_result_type(result), expected_type)

// Assert string result
#define EXPECT_XPATH_STRING(result, expected) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING); \
        char* str = taurus_xpath_result_string(result); \
        ASSERT_NE(str, nullptr); \
        EXPECT_STREQ(str, expected); \
        taurus_free_string(str); \
    } while(0)

// Assert number result
#define EXPECT_XPATH_NUMBER(result, expected) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER); \
        double num = taurus_xpath_result_number(result); \
        EXPECT_NEAR(num, expected, 0.0001); \
    } while(0)

// Assert number result (exact match)
#define EXPECT_XPATH_NUMBER_EQ(result, expected) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER); \
        double num = taurus_xpath_result_number(result); \
        EXPECT_DOUBLE_EQ(num, expected); \
    } while(0)

// Assert boolean result
#define EXPECT_XPATH_BOOLEAN(result, expected) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_BOOLEAN); \
        int boolean = taurus_xpath_result_boolean(result); \
        EXPECT_EQ(boolean, (expected) ? 1 : 0); \
    } while(0)

// Assert nodeset size
#define EXPECT_XPATH_NODESET_SIZE(result, expected) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NODESET); \
        size_t count = taurus_xpath_result_count(result); \
        EXPECT_EQ(count, static_cast<size_t>(expected)); \
    } while(0)

// Assert nodeset is empty
#define EXPECT_XPATH_EMPTY_NODESET(result) \
    EXPECT_XPATH_NODESET_SIZE(result, 0)

// Assert result is NaN
#define EXPECT_XPATH_NAN(result) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER); \
        double num = taurus_xpath_result_number(result); \
        EXPECT_TRUE(std::isnan(num)) << "Expected NaN, got " << num; \
    } while(0)

// Assert result is Infinity
#define EXPECT_XPATH_INFINITY(result) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER); \
        double num = taurus_xpath_result_number(result); \
        EXPECT_TRUE(std::isinf(num) && num > 0) << "Expected +Infinity, got " << num; \
    } while(0)

// Assert result is -Infinity
#define EXPECT_XPATH_NEG_INFINITY(result) \
    do { \
        EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER); \
        double num = taurus_xpath_result_number(result); \
        EXPECT_TRUE(std::isinf(num) && num < 0) << "Expected -Infinity, got " << num; \
    } while(0)

/**
 * Helper to get element name from nodeset result
 */
inline std::string get_nodeset_element_name(TaurusXPathResult result, size_t index) {
    TaurusElement elem = taurus_xpath_result_get(result, index);
    if (element_is_null(elem)) return "";
    const char* name = taurus_element_name(elem);
    return name ? std::string(name) : "";
}

/**
 * Helper to get element text from nodeset result
 */
inline std::string get_nodeset_element_text(TaurusXPathResult result, size_t index) {
    TaurusElement elem = taurus_xpath_result_get(result, index);
    if (element_is_null(elem)) return "";
    const char* text = taurus_element_text(elem);
    return text ? std::string(text) : "";
}

/**
 * RAII wrapper for XPath results to ensure cleanup
 */
class XPathResultGuard {
private:
    TaurusXPathResult result_;

public:
    explicit XPathResultGuard(TaurusXPathResult result) : result_(result) {}

    ~XPathResultGuard() {
        if (result_) {
            taurus_xpath_result_free(result_);
        }
    }

    // Delete copy constructor and assignment
    XPathResultGuard(const XPathResultGuard&) = delete;
    XPathResultGuard& operator=(const XPathResultGuard&) = delete;

    // Allow move
    XPathResultGuard(XPathResultGuard&& other) noexcept : result_(other.result_) {
        other.result_ = nullptr;
    }

    TaurusXPathResult get() const { return result_; }
    operator TaurusXPathResult() const { return result_; }
};

} // namespace taurus_test

#endif /* XPATH_TEST_UTILS_H */