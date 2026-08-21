# Getting Started with Leptris

This guide will help you get started with the Leptris XML parser library.

## Quick Start

### Minimal Example

```c
#include <leptris.h>

int main() {
    /* Parse an XML document from a string */
    const char* xml = "<root>Hello, World!</root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);

    if (!doc) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    /* Get the root element */
    LeptrisElement root = leptris_document_root(doc);
    const char* name = leptris_element_name(root);
    const char* text = leptris_element_text(root);

    printf("Element: %s\n", name);
    printf("Text: %s\n", text);

    /* Clean up */
    leptris_document_free(doc);
    return 0;
}
```

### Compiling Your Program

```bash
# Using pkg-config
gcc -o myprogram myprogram.c $(pkg-config --cflags --libs leptris)

# Or manually
gcc -o myprogram myprogram.c -I/usr/local/include -L/usr/local/lib -lleptris -lm
```

## What is Leptris?

Leptris is a high-performance XML parser and XPath 1.0 engine implemented in pure C. It provides:

- **Fast XML parsing**: Optimized for performance with zero-copy parsing
- **DOM API**: Tree-based navigation and manipulation
- **SAX API**: Event-driven parsing for large files
- **XPath 1.0**: Full XPath 1.0 query support
- **Small footprint**: Minimal memory usage with compact element structure

## Key Features

### Memory Management

Leptris uses pool allocation for efficient memory management:

```c
LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
/* All DOM nodes are allocated from the document's pool */
leptris_document_free(doc);  /* Frees all nodes at once */
```

### Zero-Copy Parsing

When parsing from a string, Leptris can use the original buffer without copying:

```c
/* In-place parsing - Leptris takes ownership of the buffer */
char* xml_copy = strdup(xml_string);
LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, strlen(xml_copy), NULL);
/* Don't free xml_copy - leptris_document_free() will do it */
leptris_document_free(doc);
```

### XPath Queries

Execute XPath queries on your documents:

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[price > 30]", NULL);

if (result && leptris_xpath_result_type(result) == LEPTRIS_XPATH_NODESET) {
    int count = leptris_xpath_result_nodeset_count(result);
    for (int i = 0; i < count; i++) {
        LeptrisElement elem = leptris_xpath_result_nodeset_item(result, i);
        printf("Found: %s\n", leptris_element_name(elem));
    }
}

leptris_xpath_result_free(result);
```

## Next Steps

- **Building**: See [building.md](building.md) for compilation and installation instructions
- **Parsing**: See the parsing guide for advanced parsing options
- **XPath**: See the XPath query guide for XPath examples
- **API Reference**: See `api/` directory for detailed API documentation

## Error Handling

Leptris uses status codes for error handling:

```c
const char* xml = "<root>...</root>";
LeptrisStatus status;
LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);

if (status != LEPTRIS_OK) {
    fprintf(stderr, "Parse error: %d\n", status);
    return 1;
}
```

Common status codes:
- `LEPTRIS_OK` (0) - Success
- `LEPTRIS_ERROR_MEMORY` (-1) - Memory allocation failed
- `LEPTRIS_ERROR_PARSE` (-2) - XML syntax error
- `LEPTRIS_ERROR_NULL_ARG` (-4) - NULL argument passed
- `LEPTRIS_ERROR_IO` (-7) - I/O error

## Thread Safety

Leptris is thread-safe with the following rules:

- Multiple threads can parse different documents simultaneously
- Multiple threads can query different documents with XPath simultaneously
- A document should not be modified while being accessed from multiple threads
- Each document should be freed by the same thread that created it
