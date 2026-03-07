/**
 * test_sax_basic.c - Basic SAX parser tests
 */

#include "../../../src/include/taurus/sax/sax.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test context */
typedef struct {
    int start_doc_called;
    int end_doc_called;
    int start_elem_count;
    int end_elem_count;
    int chars_count;
    char last_element[256];
    char text_content[1024];
    size_t text_len;

    /* New callback tracking */
    int comment_called;
    char last_comment[512];
    int cdata_called;
    char last_cdata[512];
    int pi_called;
    char last_pi_target[128];
    char last_pi_data[512];
    int start_ns_called;
    int end_ns_called;
    char last_ns_prefix[128];
    char last_ns_uri[512];
} TestContext;

/* Callbacks */
static void test_start_document(void* user_data) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->start_doc_called = 1;
    printf("SAX: start_document\n");
}

static void test_end_document(void* user_data) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->end_doc_called = 1;
    printf("SAX: end_document\n");
}

static void test_start_element(void* user_data, const char* name, const char** attrs) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->start_elem_count++;
    strncpy(ctx->last_element, name, sizeof(ctx->last_element) - 1);

    printf("SAX: start_element(%s", name);
    if (attrs && attrs[0]) {
        printf(", attrs=[");
        for (int i = 0; attrs[i]; i += 2) {
            printf("%s=%s", attrs[i], attrs[i + 1]);
            if (attrs[i + 2]) printf(", ");
        }
        printf("]");
    }
    printf(")\n");
}

static void test_end_element(void* user_data, const char* name) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->end_elem_count++;
    printf("SAX: end_element(%s)\n", name);
}

static void test_characters(void* user_data, const char* text, size_t len) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->chars_count++;

    if (ctx->text_len + len < sizeof(ctx->text_content) - 1) {
        memcpy(ctx->text_content + ctx->text_len, text, len);
        ctx->text_len += len;
        ctx->text_content[ctx->text_len] = '\0';
    }

    printf("SAX: characters(\"%.*s\", %zu)\n", (int)len, text, len);
}

static void test_comment(void* user_data, const char* comment) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->comment_called++;
    strncpy(ctx->last_comment, comment, sizeof(ctx->last_comment) - 1);
    printf("SAX: comment(\"%s\")\n", comment);
}

static void test_cdata(void* user_data, const char* cdata) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->cdata_called++;
    strncpy(ctx->last_cdata, cdata, sizeof(ctx->last_cdata) - 1);
    printf("SAX: cdata(\"%s\")\n", cdata);
}

static void test_processing_instruction(void* user_data, const char* target, const char* data) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->pi_called++;
    strncpy(ctx->last_pi_target, target, sizeof(ctx->last_pi_target) - 1);
    if (data) {
        strncpy(ctx->last_pi_data, data, sizeof(ctx->last_pi_data) - 1);
    } else {
        ctx->last_pi_data[0] = '\0';
    }
    printf("SAX: processing_instruction(\"%s\", \"%s\")\n", target, data ? data : "");
}

static void test_start_prefix_mapping(void* user_data, const char* prefix, const char* uri) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->start_ns_called++;
    strncpy(ctx->last_ns_prefix, prefix, sizeof(ctx->last_ns_prefix) - 1);
    strncpy(ctx->last_ns_uri, uri, sizeof(ctx->last_ns_uri) - 1);
    printf("SAX: start_prefix_mapping(\"%s\", \"%s\")\n", prefix, uri);
}

static void test_end_prefix_mapping(void* user_data, const char* prefix) {
    TestContext* ctx = (TestContext*)user_data;
    ctx->end_ns_called++;
    printf("SAX: end_prefix_mapping(\"%s\")\n", prefix);
}

/* Test cases */
void test_simple_element(void) {
    printf("\n=== Test: Simple Element ===\n");

    const char* xml = "<root>Hello World</root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.start_document = test_start_document;
    handler.end_document = test_end_document;
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;
    handler.characters = test_characters;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.start_doc_called == 1);
    assert(ctx.end_doc_called == 1);
    assert(ctx.start_elem_count == 1);
    assert(ctx.end_elem_count == 1);
    assert(strcmp(ctx.last_element, "root") == 0);
    assert(strcmp(ctx.text_content, "Hello World") == 0);

    printf("✓ Simple element test passed\n");
}

void test_nested_elements(void) {
    printf("\n=== Test: Nested Elements ===\n");

    const char* xml = "<root><item>Test</item></root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;
    handler.characters = test_characters;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.start_elem_count == 2); /* root + item */
    assert(ctx.end_elem_count == 2);
    assert(strcmp(ctx.text_content, "Test") == 0);

    printf("✓ Nested elements test passed\n");
}

void test_attributes(void) {
    printf("\n=== Test: Attributes ===\n");

    const char* xml = "<root id=\"123\" name=\"test\"/>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.start_elem_count == 1);
    assert(ctx.end_elem_count == 1);
    assert(strcmp(ctx.last_element, "root") == 0);

    printf("✓ Attributes test passed\n");
}

void test_self_closing(void) {
    printf("\n=== Test: Self-Closing Tag ===\n");

    const char* xml = "<root/>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.start_elem_count == 1);
    assert(ctx.end_elem_count == 1);

    printf("✓ Self-closing tag test passed\n");
}

void test_comment_callback(void) {
    printf("\n=== Test: Comment Callback ===\n");

    const char* xml = "<!-- This is a comment --><root>Test</root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.comment = test_comment;
    handler.start_element = test_start_element;
    handler.characters = test_characters;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.comment_called == 1);
    assert(strcmp(ctx.last_comment, " This is a comment ") == 0);

    printf("✓ Comment callback test passed\n");
}

void test_cdata_callback(void) {
    printf("\n=== Test: CDATA Callback ===\n");

    const char* xml = "<root><![CDATA[<html><body>Test</body></html>]]></root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.cdata = test_cdata;
    handler.start_element = test_start_element;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.cdata_called == 1);
    assert(strcmp(ctx.last_cdata, "<html><body>Test</body></html>") == 0);

    printf("✓ CDATA callback test passed\n");
}

void test_processing_instruction_callback(void) {
    printf("\n=== Test: Processing Instruction Callback ===\n");

    const char* xml = "<?xml-stylesheet type=\"text/xsl\" href=\"style.xsl\"?><root/>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.processing_instruction = test_processing_instruction;
    handler.start_element = test_start_element;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.pi_called == 1);
    assert(strcmp(ctx.last_pi_target, "xml-stylesheet") == 0);
    assert(strcmp(ctx.last_pi_data, "type=\"text/xsl\" href=\"style.xsl\"") == 0);

    printf("✓ Processing instruction callback test passed\n");
}

void test_namespace_callbacks(void) {
    printf("\n=== Test: Namespace Callbacks ===\n");

    const char* xml = "<root xmlns=\"http://example.com\" xmlns:foo=\"http://foo.com\"><foo:item/></root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.start_prefix_mapping = test_start_prefix_mapping;
    handler.end_prefix_mapping = test_end_prefix_mapping;
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.start_ns_called == 2); /* default + foo */
    assert(ctx.end_ns_called == 2);

    printf("✓ Namespace callbacks test passed\n");
}

void test_mixed_content(void) {
    printf("\n=== Test: Mixed Content (All Callbacks) ===\n");

    const char* xml =
        "<!-- Header comment -->"
        "<root xmlns=\"http://test.com\">"
        "  Text content"
        "  <![CDATA[<special>data</special>]]>"
        "  <?target instruction?>"
        "</root>";

    TestContext ctx = {0};
    TaurusSAXHandler handler = {0};
    handler.comment = test_comment;
    handler.cdata = test_cdata;
    handler.processing_instruction = test_processing_instruction;
    handler.start_prefix_mapping = test_start_prefix_mapping;
    handler.end_prefix_mapping = test_end_prefix_mapping;
    handler.start_element = test_start_element;
    handler.end_element = test_end_element;
    handler.characters = test_characters;

    int result = taurus_sax_parse(xml, strlen(xml), &handler, &ctx);

    assert(result == 0);
    assert(ctx.comment_called == 1);
    assert(ctx.cdata_called == 1);
    assert(ctx.pi_called == 1);
    assert(ctx.start_ns_called == 1);
    assert(ctx.end_ns_called == 1);

    printf("✓ Mixed content test passed\n");
}

int main(void) {
    printf("Running SAX Basic Tests\n");
    printf("========================\n");

    test_simple_element();
    test_nested_elements();
    test_attributes();
    test_self_closing();
    test_comment_callback();
    test_cdata_callback();
    test_processing_instruction_callback();
    test_namespace_callbacks();
    test_mixed_content();

    printf("\n========================\n");
    printf("All SAX basic tests passed! ✓\n");

    return 0;
}