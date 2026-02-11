# Getting Started with Taurus

This guide will help you get started with the Taurus XML parser library.

## Quick Start

### Minimal Example

```c
#include <taurus.h>

int main() {
    /* Parse an XML document from a string */
    const char* xml = "<root>Hello, World!</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    if (!doc) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    /* Get the root element */
    TaurusElement root = taurus_document_root(doc);
    const char* name = taurus_element_name(root);
    const char* text = taurus_element_text(root);

    printf("Element: %s\n", name);
    printf("Text: %s\n", text);

    /* Clean up */
    taurus_document_free(doc);
    return 0;
}
```

### Compiling Your Program

```bash
# Using pkg-config
gcc -o myprogram myprogram.c $(pkg-config --cflags --libs taurus)

# Or manually
gcc -o myprogram myprogram.c -I/usr/local/include -L/usr/local/lib -ltaurus -lm
```

## What is Taurus?

Taurus is a high-performance XML parser and XPath 1.0 engine implemented in pure C. It provides:

- **Fast XML parsing**: Optimized for performance with zero-copy parsing
- **DOM API**: Tree-based navigation and manipulation
- **SAX API**: Event-driven parsing for large files
- **XPath 1.0**: Full XPath 1.0 query support
- **Small footprint**: Minimal memory usage with compact element structure

## Key Features

### Memory Management

Taurus uses pool allocation for efficient memory management:

```c
TaurusDocument doc = taurus_parse_string(xml, len, NULL);
/* All DOM nodes are allocated from the document's pool */
taurus_document_free(doc);  /* Frees all nodes at once */
```

### Zero-Copy Parsing

When parsing from a string, Taurus can use the original buffer without copying:

```c
/* In-place parsing - Taurus takes ownership of the buffer */
char* xml_copy = strdup(xml_string);
TaurusDocument doc = taurus_parse_string_inplace(xml_copy, strlen(xml_copy), NULL);
/* Don't free xml_copy - taurus_document_free() will do it */
taurus_document_free(doc);
```

### XPath Queries

Execute XPath queries on your documents:

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[price > 30]", NULL);

if (result && taurus_xpath_result_type(result) == TAURUS_XPATH_NODESET) {
    int count = taurus_xpath_result_nodeset_count(result);
    for (int i = 0; i < count; i++) {
        TaurusElement elem = taurus_xpath_result_nodeset_item(result, i);
        printf("Found: %s\n", taurus_element_name(elem));
    }
}

taurus_xpath_result_free(result);
```

## Next Steps

- **Building**: See [building.md](building.md) for compilation and installation instructions
- **Parsing**: See the parsing guide for advanced parsing options
- **XPath**: See the XPath query guide for XPath examples
- **API Reference**: See `api/` directory for detailed API documentation

## Error Handling

Taurus uses status codes for error handling:

```c
const char* xml = "<root>...</root>";
TaurusStatus status;
TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);

if (status != TAURUS_OK) {
    fprintf(stderr, "Parse error: %d\n", status);
    return 1;
}
```

Common status codes:
- `TAURUS_OK` (0) - Success
- `TAURUS_ERROR_MEMORY` (-1) - Memory allocation failed
- `TAURUS_ERROR_PARSE` (-2) - XML syntax error
- `TAURUS_ERROR_NULL_ARG` (-4) - NULL argument passed
- `TAURUS_ERROR_IO` (-7) - I/O error

## Thread Safety

Taurus is thread-safe with the following rules:

- Multiple threads can parse different documents simultaneously
- Multiple threads can query different documents with XPath simultaneously
- A document should not be modified while being accessed from multiple threads
- Each document should be freed by the same thread that created it
