/**
 * Benchmark Fixture Generator
 *
 * Generates all XML test files needed for the comprehensive benchmark suite.
 * Run this once to create the fixtures, or when you need to regenerate them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define OUTPUT_DIR "data"

/* Create output directory if it doesn't exist */
static void ensure_output_dir(void) {
    struct stat st = {0};
    if (stat(OUTPUT_DIR, &st) == -1) {
        mkdir(OUTPUT_DIR, 0755);
    }
}

/* Write string to file */
static void write_file(const char* filename, const char* content) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", OUTPUT_DIR, filename);

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create %s\n", path);
        return;
    }
    fprintf(f, "%s", content);
    fclose(f);
    printf("Created: %s (%.1f KB)\n", path, (double)strlen(content) / 1024);
}

/* ============================================================================
 * Small/Medium/Large Files
 * ============================================================================ */

static void generate_small_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "  <item id=\"1\">Content 1</item>\n"
        "  <item id=\"2\">Content 2</item>\n"
        "  <item id=\"3\">Content 3</item>\n"
        "</root>\n";
    write_file("small.xml", xml);
}

static void generate_medium_xml(void) {
    /* Generate ~50KB XML with nested structure */
    char* buffer = (char*)malloc(100 * 1024);
    if (!buffer) return;

    char* p = buffer;
    p += sprintf(p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");

    for (int i = 0; i < 100; i++) {
        p += sprintf(p, "  <section id=\"%d\">\n", i);
        for (int j = 0; j < 10; j++) {
            p += sprintf(p, "    <item id=\"%d_%d\" attr1=\"value%d\" attr2=\"value%d\">\n",
                         i, j, j, j);
            p += sprintf(p, "      <name>Item %d.%d</name>\n", i, j);
            p += sprintf(p, "      <description>This is the description for item %d.%d with some text content.</description>\n", i, j);
            p += sprintf(p, "      <value>%d</value>\n", i * 10 + j);
            p += sprintf(p, "    </item>\n");
        }
        p += sprintf(p, "  </section>\n");
    }
    p += sprintf(p, "</root>\n");

    write_file("medium.xml", buffer);
    free(buffer);
}

static void generate_large_xml(void) {
    /* Generate ~5MB XML */
    FILE* f = fopen(OUTPUT_DIR "/large.xml", "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create large.xml\n");
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");

    for (int i = 0; i < 1000; i++) {
        fprintf(f, "  <section id=\"%d\">\n", i);
        for (int j = 0; j < 50; j++) {
            fprintf(f, "    <item id=\"%d_%d\" attr1=\"value%d\" attr2=\"value%d\">\n", i, j, j, j);
            fprintf(f, "      <name>Item %d.%d</name>\n", i, j);
            fprintf(f, "      <description>This is the description for item %d.%d with some text content.</description>\n", i, j);
            fprintf(f, "      <value>%d</value>\n", i * 100 + j);
            fprintf(f, "    </item>\n");
        }
        fprintf(f, "  </section>\n");
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/large.xml (~5000 KB)\n", OUTPUT_DIR);
}

/* ============================================================================
 * Deep Nesting Files
 * ============================================================================ */

static void generate_deep_100_xml(void) {
    char* buffer = (char*)malloc(100 * 1024);
    if (!buffer) return;

    char* p = buffer;
    p += sprintf(p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");

    for (int i = 0; i < 100; i++) {
        p += sprintf(p, "<level%d>\n", i);
    }
    p += sprintf(p, "<content>Deep content</content>\n");
    for (int i = 99; i >= 0; i--) {
        p += sprintf(p, "</level%d>\n", i);
    }
    p += sprintf(p, "</root>\n");

    write_file("deep_100.xml", buffer);
    free(buffer);
}

static void generate_deep_1000_xml(void) {
    FILE* f = fopen(OUTPUT_DIR "/deep_1000.xml", "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");
    for (int i = 0; i < 1000; i++) {
        fprintf(f, "<level%d>\n", i);
    }
    fprintf(f, "<content>Deep content</content>\n");
    for (int i = 999; i >= 0; i--) {
        fprintf(f, "</level%d>\n", i);
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/deep_1000.xml\n", OUTPUT_DIR);
}

/* ============================================================================
 * Wide Files (many siblings)
 * ============================================================================ */

static void generate_wide_1000_xml(void) {
    FILE* f = fopen(OUTPUT_DIR "/wide_1000.xml", "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");
    for (int i = 0; i < 1000; i++) {
        fprintf(f, "  <item id=\"%d\" name=\"item%d\" value=\"%d\">Content %d</item>\n",
                i, i, i * 10, i);
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/wide_1000.xml\n", OUTPUT_DIR);
}

static void generate_wide_10000_xml(void) {
    FILE* f = fopen(OUTPUT_DIR "/wide_10000.xml", "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");
    for (int i = 0; i < 10000; i++) {
        fprintf(f, "  <item id=\"%d\" name=\"item%d\" value=\"%d\">Content %d</item>\n",
                i, i, i * 10, i);
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/wide_10000.xml\n", OUTPUT_DIR);
}

/* ============================================================================
 * Attribute-Heavy Files
 * ============================================================================ */

static void generate_attrs_10_xml(void) {
    char* buffer = (char*)malloc(100 * 1024);
    if (!buffer) return;

    char* p = buffer;
    p += sprintf(p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");

    for (int i = 0; i < 100; i++) {
        p += sprintf(p, "  <item id=\"%d\"", i);
        for (int j = 0; j < 10; j++) {
            p += sprintf(p, " attr%d=\"value%d_%d\"", j, i, j);
        }
        p += sprintf(p, ">Content %d</item>\n", i);
    }
    p += sprintf(p, "</root>\n");

    write_file("attrs_10.xml", buffer);
    free(buffer);
}

static void generate_attrs_100_xml(void) {
    FILE* f = fopen(OUTPUT_DIR "/attrs_100.xml", "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");
    for (int i = 0; i < 50; i++) {
        fprintf(f, "  <item id=\"%d\"", i);
        for (int j = 0; j < 100; j++) {
            fprintf(f, " attr%d=\"value%d_%d\"", j, i, j);
        }
        fprintf(f, ">Content %d</item>\n", i);
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/attrs_100.xml\n", OUTPUT_DIR);
}

static void generate_attrs_1000_xml(void) {
    FILE* f = fopen(OUTPUT_DIR "/attrs_1000.xml", "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n");
    for (int i = 0; i < 10; i++) {
        fprintf(f, "  <item id=\"%d\"", i);
        for (int j = 0; j < 1000; j++) {
            fprintf(f, " attr%d=\"value%d_%d\"", j, i, j);
        }
        fprintf(f, ">Content %d</item>\n", i);
    }
    fprintf(f, "</root>\n");
    fclose(f);
    printf("Created: %s/attrs_1000.xml\n", OUTPUT_DIR);
}

/* ============================================================================
 * Special Content Files
 * ============================================================================ */

static void generate_namespaces_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root xmlns=\"http://default.namespace.org\"\n"
        "      xmlns:ns1=\"http://namespace1.org\"\n"
        "      xmlns:ns2=\"http://namespace2.org\"\n"
        "      xmlns:ns3=\"http://namespace3.org\"\n"
        "      xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
        "  <ns1:element ns1:attr=\"value1\">Content 1</ns1:element>\n"
        "  <ns2:element ns2:attr=\"value2\">Content 2</ns2:element>\n"
        "  <ns3:element ns3:attr=\"value3\">Content 3</ns3:element>\n"
        "  <mixed ns1:attr1=\"v1\" ns2:attr2=\"v2\" attr3=\"v3\">Mixed</mixed>\n"
        "  <nested>\n"
        "    <ns1:child ns1:id=\"1\">Child 1</ns1:child>\n"
        "    <ns2:child ns2:id=\"2\">Child 2</ns2:child>\n"
        "  </nested>\n"
        "</root>\n";
    write_file("namespaces.xml", xml);
}

static void generate_mixed_content_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "  Text before element\n"
        "  <element>Element content</element>\n"
        "  Text between elements\n"
        "  <nested>\n"
        "    Nested text\n"
        "    <child>Child content</child>\n"
        "    More nested text\n"
        "  </nested>\n"
        "  Final text\n"
        "</root>\n";
    write_file("mixed_content.xml", xml);
}

static void generate_entities_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE root [\n"
        "  <!ENTITY lt \"&#60;\">\n"
        "  <!ENTITY gt \"&#62;\">\n"
        "  <!ENTITY amp \"&#38;\">\n"
        "  <!ENTITY quot \"&#34;\">\n"
        "  <!ENTITY apos \"&#39;\">\n"
        "]>\n"
        "<root>\n"
        "  <item>Less than: &lt;</item>\n"
        "  <item>Greater than: &gt;</item>\n"
        "  <item>Ampersand: &amp;</item>\n"
        "  <item>Quote: &quot;</item>\n"
        "  <item>Apostrophe: &apos;</item>\n"
        "  <item>All: &lt; &gt; &amp; &quot; &apos;</item>\n"
        "  <item attr=\"&lt;escaped&;\">Attribute with entities</item>\n"
        "</root>\n";
    write_file("entities.xml", xml);
}

static void generate_cdata_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "  <script><![CDATA[\n"
        "    if (x < 10 && y > 5) {\n"
        "      console.log(\"Hello & goodbye\");\n"
        "    }\n"
        "  ]]></script>\n"
        "  <style><![CDATA[\n"
        "    .class { color: red; }\n"
        "    @media (max-width: 768px) { }\n"
        "  ]]></style>\n"
        "  <code><![CDATA[\n"
        "    <html><body>Raw HTML</body></html>\n"
        "  ]]></code>\n"
        "  <mixed>Text before <![CDATA[CDATA content]]> text after</mixed>\n"
        "</root>\n";
    write_file("cdata.xml", xml);
}

static void generate_unicode_xml(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "  <ascii>ASCII text: Hello World</ascii>\n"
        "  <latin1>Latin-1: café résumé naïve</latin1>\n"
        "  <greek>Greek: ΑΒΓΔ ΕΖΗΘ</greek>\n"
        "  <cyrillic>Cyrillic: Абвгд</cyrillic>\n"
        "  <chinese>Chinese: 中文测试</chinese>\n"
        "  <japanese>Japanese: 日本語テスト</japanese>\n"
        "  <korean>Korean: 한국어 테스트</korean>\n"
        "  <emoji>Emoji: 😀🎉🚀💻</emoji>\n"
        "  <mixed>Mix: Hello 世界 🌍</mixed>\n"
        "  <arabic>Arabic: مرحبا</arabic>\n"
        "  <hebrew>Hebrew: שלום</hebrew>\n"
        "</root>\n";
    write_file("unicode.xml", xml);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== Taurus Benchmark Fixture Generator ===\n\n");

    ensure_output_dir();

    printf("--- Size variants ---\n");
    generate_small_xml();
    generate_medium_xml();
    generate_large_xml();

    printf("\n--- Deep nesting ---\n");
    generate_deep_100_xml();
    generate_deep_1000_xml();

    printf("\n--- Wide documents ---\n");
    generate_wide_1000_xml();
    generate_wide_10000_xml();

    printf("\n--- Attribute-heavy ---\n");
    generate_attrs_10_xml();
    generate_attrs_100_xml();
    generate_attrs_1000_xml();

    printf("\n--- Special content ---\n");
    generate_namespaces_xml();
    generate_mixed_content_xml();
    generate_entities_xml();
    generate_cdata_xml();
    generate_unicode_xml();

    printf("\n=== All fixtures generated in %s/ ===\n", OUTPUT_DIR);

    return 0;
}
