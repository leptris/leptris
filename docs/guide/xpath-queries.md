# XPath Queries

This guide covers how to use XPath 1.0 queries with Leptris.

## Basic XPath Query

```c
#include <leptris.h>

int main() {
    const char* xml = "<books><book><title>XML Guide</title></book></books>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);

    /* Execute XPath query */
    LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book", NULL);

    if (!result) {
        fprintf(stderr, "XPath query failed\n");
        leptris_document_free(doc);
        return 1;
    }

    /* Process results */
    if (leptris_xpath_result_type(result) == LEPTRIS_XPATH_NODESET) {
        int count = leptris_xpath_result_nodeset_count(result);
        printf("Found %d nodes\n", count);

        for (int i = 0; i < count; i++) {
            LeptrisElement elem = leptris_xpath_result_nodeset_item(result, i);
            printf("Element: %s\n", leptris_element_name(elem));
        }
    }

    /* Clean up */
    leptris_xpath_result_free(result);
    leptris_document_free(doc);
    return 0;
}
```

## XPath Result Types

XPath queries can return different types of results:

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, xpath_expr, NULL);

switch (leptris_xpath_result_type(result)) {
    case LEPTRIS_XPATH_NODESET:
        /* Node set - use nodeset functions */
        break;
    case LEPTRIS_XPATH_BOOLEAN:
        /* Boolean value */
        int value = leptris_xpath_result_as_boolean(result);
        break;
    case LEPTRIS_XPATH_NUMBER:
        /* Numeric value */
        double value = leptris_xpath_result_as_number(result);
        break;
    case LEPTRIS_XPATH_STRING:
        /* String value */
        const char* value = leptris_xpath_result_as_string(result);
        break;
}
```

## Location Paths

### Absolute Path

```c
/* Select from root */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "/books/book/title", NULL);
```

### Relative Path

```c
/* Select from current context */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "book/title", NULL);
```

### Wildcard

```c
/* Select all elements at this level */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "book/*", NULL);
```

### Recursive Search

```c
/* Select all book elements anywhere in document */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book", NULL);
```

## Predicates

### Index Predicate

```c
/* First book */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[1]", NULL);

/* Last book */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[last()]", NULL);
```

### Attribute Predicate

```c
/* Book with id="b1" */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[@id='b1']", NULL);
```

### Content Predicate

```c
/* Book with price > 30 */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[price > 30]", NULL);

/* Book containing "XML" in title */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[contains(title, 'XML')]", NULL);
```

### Multiple Conditions

```c
/* Book with price > 30 and category="web" */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[price > 30 and @category='web']", NULL);
```

## Axes

### Child Axis (default)

```c
/* Explicit child axis */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "child::book", NULL);

/* Same as: */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "book", NULL);
```

### Descendant Axis

```c
/* All descendants */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "descendant::title", NULL);

/* Same as: */
LeptrisXPathResult* result = leptris_xpath_eval(doc, ".//title", NULL);
```

### Parent Axis

```c
/* Parent of current element */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "..", NULL);
```

### Attribute Axis

```c
/* Select attributes */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book/@id", NULL);
```

### Ancestor Axis

```c
/* All ancestors of current element */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "ancestor::books", NULL);
```

## Functions

### String Functions

```c
/* Contains */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[contains(title, 'XML')]", NULL);

/* Starts with */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[starts-with(title, 'The')]", NULL);

/* String length */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[string-length(title) > 10]", NULL);

/* Concatenate */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "concat(title, ' - ', author)", NULL);
```

### Numeric Functions

```c
/* Sum */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "sum(//book/price)", NULL);

/* Count */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "count(//book)", NULL);

/* Floor, ceiling, round */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "floor(//book/price)", NULL);
```

### Node Functions

```c
/* Position */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[position() <= 3]", NULL);

/* Last */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[last()]", NULL);

/* Local name (without namespace prefix) */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//*[local-name()='book']", NULL);
```

### Boolean Functions

```c
/* Not */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[not(price)]", NULL);

/* True */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[price > 100 and true()]", NULL);
```

## Namespaces in XPath

### Using Namespace Context

```c
/* Document with namespaces: */
const char* xml = "<xhtml:html xmlns:xhtml='http://www.w3.org/1999/xhtml'>"
                  "  <xhtml:body>Content</xhtml:body>"
                  "</xhtml:html>";

LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);

/* Create namespace context for XPath */
LeptrisXPathContext* ctx = leptris_xpath_context_new(doc);
leptris_xpath_context_register_namespace(ctx, "xhtml",
                                        "http://www.w3.org/1999/xhtml");

/* Query with namespace prefix */
LeptrisXPathResult* result = leptris_xpath_eval_with_context(
    ctx, "//xhtml:body");

leptris_xpath_context_free(ctx);
```

### Default Namespace

Note that unprefixed element names in XPath always refer to elements in no namespace, even if a default namespace is in effect:

```c
/* This won't match elements in a default namespace */
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//body", NULL);

/* Use local-name() to match regardless of namespace */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//*[local-name()='body']", NULL);
```

## Common Query Patterns

### Find Element by ID

```c
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//*[@id='b1']", NULL);
```

### Find Elements with Specific Text

```c
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[title='XML Guide']", NULL);
```

### Find Elements Containing Text

```c
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[contains(text(), 'XML')]", NULL);
```

### Find Elements by Attribute Value

```c
/* Exact match */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[@category='web']", NULL);

/* Attribute exists */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[@id]", NULL);

/* Multiple possible values */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[@category='web' or @category='database']", NULL);
```

### Find Siblings

```c
/* Following siblings */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[1]/following-sibling::book", NULL);

/* Preceding siblings */
LeptrisXPathResult* result = leptris_xpath_eval(
    doc, "//book[3]/preceding-sibling::book", NULL);
```

## Performance Tips

1. **Use specific paths** instead of // when possible:
   ```c
   /* Faster - direct path */
   leptris_xpath_eval(doc, "/books/book/title", NULL);

   /* Slower - searches entire tree */
   leptris_xpath_eval(doc, "//title", NULL);
   ```

2. **Use predicates early** to reduce search space:
   ```c
   /* Better */
   leptris_xpath_eval(doc, "//book[@id='b1']/title", NULL);

   /* Works but less efficient */
   leptris_xpath_eval(doc, "//title[parent::book/@id='b1']", NULL);
   ```

3. **Cache results** when using the same query multiple times

4. **Use index predicates** instead of functions when possible:
   ```c
   /* Better */
   leptris_xpath_eval(doc, "//book[1]", NULL);

   /* Slower */
   leptris_xpath_eval(doc, "//book[position()=1]", NULL);
   ```

## Error Handling

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book[@id]", NULL);

if (!result) {
    fprintf(stderr, "XPath query failed (invalid expression or memory error)\n");
    return;
}

/* Check if result is a node set before accessing nodes */
if (leptris_xpath_result_type(result) != LEPTRIS_XPATH_NODESET) {
    fprintf(stderr, "Expected node set result\n");
    leptris_xpath_result_free(result);
    return;
}
```

## XPath Variables

For advanced usage, you can use XPath variables:

```c
LeptrisXPathContext* ctx = leptris_xpath_context_new(doc);
leptris_xpath_context_set_variable(ctx, "min_price", "30.0");

LeptrisXPathResult* result = leptris_xpath_eval_with_context(
    ctx, "//book[price > $min_price]");

leptris_xpath_context_free(ctx);
```

## Examples

### Extract All Book Titles

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "//book/title/text()", NULL);

if (leptris_xpath_result_type(result) == LEPTRIS_XPATH_NODESET) {
    int count = leptris_xpath_result_nodeset_count(result);
    for (int i = 0; i < count; i++) {
        LeptrisNode* node = leptris_xpath_result_nodeset_item(result, i);
        if (leptris_node_type(node) == LEPTRIS_NODE_TEXT) {
            LeptrisText text = (LeptrisText)node;
            printf("Title: %s\n", leptris_text_value(text));
        }
    }
}
```

### Calculate Total Price

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "sum(//book/price)", NULL);

if (leptris_xpath_result_type(result) == LEPTRIS_XPATH_NUMBER) {
    double total = leptris_xpath_result_as_number(result);
    printf("Total price: %.2f\n", total);
}
```

### Count Elements

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "count(//book)", NULL);

if (leptris_xpath_result_type(result) == LEPTRIS_XPATH_NUMBER) {
    double count = leptris_xpath_result_as_number(result);
    printf("Found %.0f books\n", count);
}
```

### Check Existence

```c
LeptrisXPathResult* result = leptris_xpath_eval(doc, "count(//book[@id='x']) > 0", NULL);

if (leptris_xpath_result_type(result) == LEPTRIS_XPATH_BOOLEAN) {
    int exists = leptris_xpath_result_as_boolean(result);
    printf("Book exists: %s\n", exists ? "yes" : "no");
}
```
