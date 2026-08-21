# Parsing XML Documents

This guide covers how to parse XML documents using Leptris.

## Parsing from String

### Basic String Parsing

```c
#include <leptris.h>

int main() {
    const char* xml = "<root><child>Content</child></root>";
    size_t len = strlen(xml);

    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);

    if (!doc) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    /* Use the document... */
    leptris_document_free(doc);
    return 0;
}
```

### String Parsing with Error Handling

```c
const char* xml = "<root>...</root>";
LeptrisStatus status;
LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);

if (status != LEPTRIS_OK) {
    fprintf(stderr, "Parse error %d: %s\n", status,
            leptris_error_string(status));
    return 1;
}
```

## Parsing from File

### Basic File Parsing

```c
LeptrisStatus status;
LeptrisDocument doc = leptris_parse_file("document.xml", &status);

if (!doc) {
    fprintf(stderr, "Failed to open file: %d\n", status);
    return 1;
}

/* Use the document... */
leptris_document_free(doc);
```

### File Parsing with Error Handling

```c
const char* filepath = "/path/to/document.xml";
LeptrisStatus status;
LeptrisDocument doc = leptris_parse_file(filepath, &status);

if (status != LEPTRIS_OK) {
    switch (status) {
        case LEPTRIS_ERROR_IO:
            fprintf(stderr, "File not found: %s\n", filepath);
            break;
        case LEPTRIS_ERROR_PARSE:
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

/* Parse in-place - Leptris takes ownership of xml_copy */
LeptrisDocument doc = leptris_parse_string_inplace(xml_copy, len, NULL);

if (!doc) {
    free(xml_copy);  /* Free on error */
    return 1;
}

/* Document owns xml_copy - don't free it manually */
leptris_document_free(doc);  /* This will free xml_copy */
```

**Important**: When using `leptris_parse_string_inplace`, you must:
1. Allocate the buffer with `malloc()` (not stack, not const)
2. Never free the buffer yourself (Leptris owns it)
3. Let `leptris_document_free()` handle cleanup

## Parse Options

### Namespace Processing

```c
/* Parse with namespace context (for XPath queries) */
LeptrisNamespace ns[] = {
    {"xhtml", "http://www.w3.org/1999/xhtml"},
    {"xs", "http://www.w3.org/2001/XMLSchema"},
    {NULL, NULL}  /* Terminator */
};

LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
/* Namespaces are automatically extracted during parsing */
```

### Encoding Handling

Leptris supports UTF-8 natively. For other encodings:

```c
/* When compiled with iconv support */
LeptrisDocument doc = leptris_parse_file("latin1.xml", NULL);
/* Leptris detects encoding from XML declaration */
```

## Accessing Document Content

### Get Root Element

```c
LeptrisElement root = leptris_document_root(doc);
const char* root_name = leptris_element_name(root);
printf("Root: %s\n", root_name);
```

### Traverse Children

```c
LeptrisElement root = leptris_document_root(doc);
LeptrisNode* child = leptris_node_first_child((LeptrisNode*)root);

while (child) {
    if (leptris_node_type(child) == LEPTRIS_NODE_ELEMENT) {
        LeptrisElement elem = (LeptrisElement)child;
        const char* name = leptris_element_name(elem);
        printf("Child: %s\n", name);
    }
    child = leptris_node_next_sibling(child);
}
```

### Get Element Attributes

```c
LeptrisElement elem = /* ... */;
const char* id = leptris_element_attribute(elem, "id");
if (id) {
    printf("ID: %s\n", id);
}
```

### Get Element Text Content

```c
LeptrisElement elem = /* ... */;
const char* text = leptris_element_text(elem);
if (text) {
    printf("Text: %s\n", text);
}
```

## Working with Document Type

```c
LeptrisDoctype doctype = leptris_document_doctype(doc);
if (doctype) {
    const char* name = leptris_doctype_name(doctype);
    const char* public_id = leptris_doctype_public_id(doctype);
    const char* system_id = leptris_doctype_system_id(doctype);
    printf("DOCTYPE: %s\n", name);
}
```

## Handling XML Declaration

```c
/* Check for XML declaration */
const char* version = leptris_document_version(doc);
if (version) {
    const char* encoding = leptris_document_encoding(doc);
    int standalone = leptris_document_standalone(doc);
    printf("XML %s, encoding=%s, standalone=%d\n",
           version, encoding, standalone);
}
```

## Memory Management

### Pool Allocation

All DOM nodes are allocated from the document's memory pool:

```c
LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
/* All elements, attributes, text nodes are in the pool */

/* Free everything at once */
leptris_document_free(doc);
```

### String Lifetime

Strings returned by Leptris (names, text content, attribute values) are owned by the document and are freed when the document is freed:

```c
const char* text = leptris_element_text(elem);
/* text is valid until doc is freed */
leptris_document_free(doc);
/* text is now invalid - don't use it */
```

## Common Patterns

### Iterate Over Elements by Name

```c
void iterate_elements(LeptrisElement parent, const char* name) {
    LeptrisNode* child = leptris_node_first_child((LeptrisNode*)parent);

    while (child) {
        if (leptris_node_type(child) == LEPTRIS_NODE_ELEMENT) {
            LeptrisElement elem = (LeptrisElement)child;
            const char* elem_name = leptris_element_name(elem);

            if (strcmp(elem_name, name) == 0) {
                /* Found matching element */
                printf("Found: %s\n", leptris_element_text(elem));
            }

            /* Recurse into children */
            iterate_elements(elem, name);
        }
        child = leptris_node_next_sibling(child);
    }
}
```

### Find Element by ID

```c
LeptrisElement find_element_by_id(LeptrisElement root, const char* id) {
    /* Check this element */
    const char* elem_id = leptris_element_attribute(root, "id");
    if (elem_id && strcmp(elem_id, id) == 0) {
        return root;
    }

    /* Search children */
    LeptrisNode* child = leptris_node_first_child((LeptrisNode*)root);
    while (child) {
        if (leptris_node_type(child) == LEPTRIS_NODE_ELEMENT) {
            LeptrisElement result = find_element_by_id((LeptrisElement)child, id);
            if (result) return result;
        }
        child = leptris_node_next_sibling(child);
    }

    return NULL;
}
```

### Find Elements by Attribute

```c
void find_by_attribute(LeptrisElement parent,
                       const char* attr_name,
                       const char* attr_value) {
    LeptrisNode* child = leptris_node_first_child((LeptrisNode*)parent);

    while (child) {
        if (leptris_node_type(child) == LEPTRIS_NODE_ELEMENT) {
            LeptrisElement elem = (LeptrisElement)child;
            const char* value = leptris_element_attribute(elem, attr_name);

            if (value && strcmp(value, attr_value) == 0) {
                printf("Match: %s\n", leptris_element_name(elem));
            }

            /* Recurse */
            find_by_attribute(elem, attr_name, attr_value);
        }
        child = leptris_node_next_sibling(child);
    }
}
```

## Performance Tips

1. **Use in-place parsing** for string data when possible
2. **Reuse documents** instead of reparsing the same data
3. **Use XPath** for complex queries instead of manual traversal
4. **Avoid unnecessary string copies** - use the provided pointers directly

## Error Recovery

Leptris follows the XML 1.0 specification for error handling. By default, parsing stops on the first error. For more lenient parsing, consider using the SAX API with custom error handling.
