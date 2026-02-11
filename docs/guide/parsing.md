# Parsing XML Documents

This guide covers how to parse XML documents using Taurus.

## Parsing from String

### Basic String Parsing

```c
#include <taurus.h>

int main() {
    const char* xml = "<root><child>Content</child></root>";
    size_t len = strlen(xml);

    TaurusDocument doc = taurus_parse_string(xml, len, NULL);

    if (!doc) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    /* Use the document... */
    taurus_document_free(doc);
    return 0;
}
```

### String Parsing with Error Handling

```c
const char* xml = "<root>...</root>";
TaurusStatus status;
TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);

if (status != TAURUS_OK) {
    fprintf(stderr, "Parse error %d: %s\n", status,
            taurus_error_string(status));
    return 1;
}
```

## Parsing from File

### Basic File Parsing

```c
TaurusStatus status;
TaurusDocument doc = taurus_parse_file("document.xml", &status);

if (!doc) {
    fprintf(stderr, "Failed to open file: %d\n", status);
    return 1;
}

/* Use the document... */
taurus_document_free(doc);
```

### File Parsing with Error Handling

```c
const char* filepath = "/path/to/document.xml";
TaurusStatus status;
TaurusDocument doc = taurus_parse_file(filepath, &status);

if (status != TAURUS_OK) {
    switch (status) {
        case TAURUS_ERROR_IO:
            fprintf(stderr, "File not found: %s\n", filepath);
            break;
        case TAURUS_ERROR_PARSE:
            fprintf(stderr, "Invalid XML in file: %s\n", filepath);
            break;
        default:
            fprintf(stderr, "Error %d reading file\n", status);
    }
    return 1;
}
```

## In-Place Parsing (Zero-Copy)

For maximum performance, use in-place parsing:

```c
/* Allocate a copy of the XML string */
size_t len = strlen(xml_string);
char* xml_copy = malloc(len + 1);
strcpy(xml_copy, xml_string);

/* Parse in-place - Taurus takes ownership of xml_copy */
TaurusDocument doc = taurus_parse_string_inplace(xml_copy, len, NULL);

if (!doc) {
    free(xml_copy);  /* Free on error */
    return 1;
}

/* Document owns xml_copy - don't free it manually */
taurus_document_free(doc);  /* This will free xml_copy */
```

**Important**: When using `taurus_parse_string_inplace`, you must:
1. Allocate the buffer with `malloc()` (not stack, not const)
2. Never free the buffer yourself (Taurus owns it)
3. Let `taurus_document_free()` handle cleanup

## Parse Options

### Namespace Processing

```c
/* Parse with namespace context (for XPath queries) */
TaurusNamespace ns[] = {
    {"xhtml", "http://www.w3.org/1999/xhtml"},
    {"xs", "http://www.w3.org/2001/XMLSchema"},
    {NULL, NULL}  /* Terminator */
};

TaurusDocument doc = taurus_parse_string(xml, len, NULL);
/* Namespaces are automatically extracted during parsing */
```

### Encoding Handling

Taurus supports UTF-8 natively. For other encodings:

```c
/* When compiled with iconv support */
TaurusDocument doc = taurus_parse_file("latin1.xml", NULL);
/* Taurus detects encoding from XML declaration */
```

## Accessing Document Content

### Get Root Element

```c
TaurusElement root = taurus_document_root(doc);
const char* root_name = taurus_element_name(root);
printf("Root: %s\n", root_name);
```

### Traverse Children

```c
TaurusElement root = taurus_document_root(doc);
TaurusNode* child = taurus_node_first_child((TaurusNode*)root);

while (child) {
    if (taurus_node_type(child) == TAURUS_NODE_ELEMENT) {
        TaurusElement elem = (TaurusElement)child;
        const char* name = taurus_element_name(elem);
        printf("Child: %s\n", name);
    }
    child = taurus_node_next_sibling(child);
}
```

### Get Element Attributes

```c
TaurusElement elem = /* ... */;
const char* id = taurus_element_attribute(elem, "id");
if (id) {
    printf("ID: %s\n", id);
}
```

### Get Element Text Content

```c
TaurusElement elem = /* ... */;
const char* text = taurus_element_text(elem);
if (text) {
    printf("Text: %s\n", text);
}
```

## Working with Document Type

```c
TaurusDoctype doctype = taurus_document_doctype(doc);
if (doctype) {
    const char* name = taurus_doctype_name(doctype);
    const char* public_id = taurus_doctype_public_id(doctype);
    const char* system_id = taurus_doctype_system_id(doctype);
    printf("DOCTYPE: %s\n", name);
}
```

## Handling XML Declaration

```c
/* Check for XML declaration */
const char* version = taurus_document_version(doc);
if (version) {
    const char* encoding = taurus_document_encoding(doc);
    int standalone = taurus_document_standalone(doc);
    printf("XML %s, encoding=%s, standalone=%d\n",
           version, encoding, standalone);
}
```

## Memory Management

### Pool Allocation

All DOM nodes are allocated from the document's memory pool:

```c
TaurusDocument doc = taurus_parse_string(xml, len, NULL);
/* All elements, attributes, text nodes are in the pool */

/* Free everything at once */
taurus_document_free(doc);
```

### String Lifetime

Strings returned by Taurus (names, text content, attribute values) are owned by the document and are freed when the document is freed:

```c
const char* text = taurus_element_text(elem);
/* text is valid until doc is freed */
taurus_document_free(doc);
/* text is now invalid - don't use it */
```

## Common Patterns

### Iterate Over Elements by Name

```c
void iterate_elements(TaurusElement parent, const char* name) {
    TaurusNode* child = taurus_node_first_child((TaurusNode*)parent);

    while (child) {
        if (taurus_node_type(child) == TAURUS_NODE_ELEMENT) {
            TaurusElement elem = (TaurusElement)child;
            const char* elem_name = taurus_element_name(elem);

            if (strcmp(elem_name, name) == 0) {
                /* Found matching element */
                printf("Found: %s\n", taurus_element_text(elem));
            }

            /* Recurse into children */
            iterate_elements(elem, name);
        }
        child = taurus_node_next_sibling(child);
    }
}
```

### Find Element by ID

```c
TaurusElement find_element_by_id(TaurusElement root, const char* id) {
    /* Check this element */
    const char* elem_id = taurus_element_attribute(root, "id");
    if (elem_id && strcmp(elem_id, id) == 0) {
        return root;
    }

    /* Search children */
    TaurusNode* child = taurus_node_first_child((TaurusNode*)root);
    while (child) {
        if (taurus_node_type(child) == TAURUS_NODE_ELEMENT) {
            TaurusElement result = find_element_by_id((TaurusElement)child, id);
            if (result) return result;
        }
        child = taurus_node_next_sibling(child);
    }

    return NULL;
}
```

### Find Elements by Attribute

```c
void find_by_attribute(TaurusElement parent,
                       const char* attr_name,
                       const char* attr_value) {
    TaurusNode* child = taurus_node_first_child((TaurusNode*)parent);

    while (child) {
        if (taurus_node_type(child) == TAURUS_NODE_ELEMENT) {
            TaurusElement elem = (TaurusElement)child;
            const char* value = taurus_element_attribute(elem, attr_name);

            if (value && strcmp(value, attr_value) == 0) {
                printf("Match: %s\n", taurus_element_name(elem));
            }

            /* Recurse */
            find_by_attribute(elem, attr_name, attr_value);
        }
        child = taurus_node_next_sibling(child);
    }
}
```

## Performance Tips

1. **Use in-place parsing** for string data when possible
2. **Reuse documents** instead of reparsing the same data
3. **Use XPath** for complex queries instead of manual traversal
4. **Avoid unnecessary string copies** - use the provided pointers directly

## Error Recovery

Taurus follows the XML 1.0 specification for error handling. By default, parsing stops on the first error. For more lenient parsing, consider using the SAX API with custom error handling.
