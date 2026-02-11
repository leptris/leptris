# XPath Queries

This guide covers how to use XPath 1.0 queries with Taurus.

## Basic XPath Query

```c
#include <taurus.h>

int main() {
    const char* xml = "<books><book><title>XML Guide</title></book></books>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    /* Execute XPath query */
    TaurusXPathResult* result = taurus_xpath_eval(doc, "//book", NULL);

    if (!result) {
        fprintf(stderr, "XPath query failed\n");
        taurus_document_free(doc);
        return 1;
    }

    /* Process results */
    if (taurus_xpath_result_type(result) == TAURUS_XPATH_NODESET) {
        int count = taurus_xpath_result_nodeset_count(result);
        printf("Found %d nodes\n", count);

        for (int i = 0; i < count; i++) {
            TaurusElement elem = taurus_xpath_result_nodeset_item(result, i);
            printf("Element: %s\n", taurus_element_name(elem));
        }
    }

    /* Clean up */
    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return 0;
}
```

## XPath Result Types

XPath queries can return different types of results:

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, xpath_expr, NULL);

switch (taurus_xpath_result_type(result)) {
    case TAURUS_XPATH_NODESET:
        /* Node set - use nodeset functions */
        break;
    case TAURUS_XPATH_BOOLEAN:
        /* Boolean value */
        int value = taurus_xpath_result_as_boolean(result);
        break;
    case TAURUS_XPATH_NUMBER:
        /* Numeric value */
        double value = taurus_xpath_result_as_number(result);
        break;
    case TAURUS_XPATH_STRING:
        /* String value */
        const char* value = taurus_xpath_result_as_string(result);
        break;
}
```

## Location Paths

### Absolute Path

```c
/* Select from root */
TaurusXPathResult* result = taurus_xpath_eval(doc, "/books/book/title", NULL);
```

### Relative Path

```c
/* Select from current context */
TaurusXPathResult* result = taurus_xpath_eval(doc, "book/title", NULL);
```

### Wildcard

```c
/* Select all elements at this level */
TaurusXPathResult* result = taurus_xpath_eval(doc, "book/*", NULL);
```

### Recursive Search

```c
/* Select all book elements anywhere in document */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book", NULL);
```

## Predicates

### Index Predicate

```c
/* First book */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[1]", NULL);

/* Last book */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[last()]", NULL);
```

### Attribute Predicate

```c
/* Book with id="b1" */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[@id='b1']", NULL);
```

### Content Predicate

```c
/* Book with price > 30 */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[price > 30]", NULL);

/* Book containing "XML" in title */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[contains(title, 'XML')]", NULL);
```

### Multiple Conditions

```c
/* Book with price > 30 and category="web" */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[price > 30 and @category='web']", NULL);
```

## Axes

### Child Axis (default)

```c
/* Explicit child axis */
TaurusXPathResult* result = taurus_xpath_eval(doc, "child::book", NULL);

/* Same as: */
TaurusXPathResult* result = taurus_xpath_eval(doc, "book", NULL);
```

### Descendant Axis

```c
/* All descendants */
TaurusXPathResult* result = taurus_xpath_eval(doc, "descendant::title", NULL);

/* Same as: */
TaurusXPathResult* result = taurus_xpath_eval(doc, ".//title", NULL);
```

### Parent Axis

```c
/* Parent of current element */
TaurusXPathResult* result = taurus_xpath_eval(doc, "..", NULL);
```

### Attribute Axis

```c
/* Select attributes */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book/@id", NULL);
```

### Ancestor Axis

```c
/* All ancestors of current element */
TaurusXPathResult* result = taurus_xpath_eval(doc, "ancestor::books", NULL);
```

## Functions

### String Functions

```c
/* Contains */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[contains(title, 'XML')]", NULL);

/* Starts with */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[starts-with(title, 'The')]", NULL);

/* String length */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[string-length(title) > 10]", NULL);

/* Concatenate */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "concat(title, ' - ', author)", NULL);
```

### Numeric Functions

```c
/* Sum */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "sum(//book/price)", NULL);

/* Count */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "count(//book)", NULL);

/* Floor, ceiling, round */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "floor(//book/price)", NULL);
```

### Node Functions

```c
/* Position */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[position() <= 3]", NULL);

/* Last */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[last()]", NULL);

/* Local name (without namespace prefix) */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//*[local-name()='book']", NULL);
```

### Boolean Functions

```c
/* Not */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[not(price)]", NULL);

/* True */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[price > 100 and true()]", NULL);
```

## Namespaces in XPath

### Using Namespace Context

```c
/* Document with namespaces: */
const char* xml = "<xhtml:html xmlns:xhtml='http://www.w3.org/1999/xhtml'>"
                  "  <xhtml:body>Content</xhtml:body>"
                  "</xhtml:html>";

TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

/* Create namespace context for XPath */
TaurusXPathContext* ctx = taurus_xpath_context_new(doc);
taurus_xpath_context_register_namespace(ctx, "xhtml",
                                        "http://www.w3.org/1999/xhtml");

/* Query with namespace prefix */
TaurusXPathResult* result = taurus_xpath_eval_with_context(
    ctx, "//xhtml:body");

taurus_xpath_context_free(ctx);
```

### Default Namespace

Note that unprefixed element names in XPath always refer to elements in no namespace, even if a default namespace is in effect:

```c
/* This won't match elements in a default namespace */
TaurusXPathResult* result = taurus_xpath_eval(doc, "//body", NULL);

/* Use local-name() to match regardless of namespace */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//*[local-name()='body']", NULL);
```

## Common Query Patterns

### Find Element by ID

```c
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//*[@id='b1']", NULL);
```

### Find Elements with Specific Text

```c
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[title='XML Guide']", NULL);
```

### Find Elements Containing Text

```c
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[contains(text(), 'XML')]", NULL);
```

### Find Elements by Attribute Value

```c
/* Exact match */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[@category='web']", NULL);

/* Attribute exists */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[@id]", NULL);

/* Multiple possible values */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[@category='web' or @category='database']", NULL);
```

### Find Siblings

```c
/* Following siblings */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[1]/following-sibling::book", NULL);

/* Preceding siblings */
TaurusXPathResult* result = taurus_xpath_eval(
    doc, "//book[3]/preceding-sibling::book", NULL);
```

## Performance Tips

1. **Use specific paths** instead of // when possible:
   ```c
   /* Faster - direct path */
   taurus_xpath_eval(doc, "/books/book/title", NULL);

   /* Slower - searches entire tree */
   taurus_xpath_eval(doc, "//title", NULL);
   ```

2. **Use predicates early** to reduce search space:
   ```c
   /* Better */
   taurus_xpath_eval(doc, "//book[@id='b1']/title", NULL);

   /* Works but less efficient */
   taurus_xpath_eval(doc, "//title[parent::book/@id='b1']", NULL);
   ```

3. **Cache results** when using the same query multiple times

4. **Use index predicates** instead of functions when possible:
   ```c
   /* Better */
   taurus_xpath_eval(doc, "//book[1]", NULL);

   /* Slower */
   taurus_xpath_eval(doc, "//book[position()=1]", NULL);
   ```

## Error Handling

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book[@id]", NULL);

if (!result) {
    fprintf(stderr, "XPath query failed (invalid expression or memory error)\n");
    return;
}

/* Check if result is a node set before accessing nodes */
if (taurus_xpath_result_type(result) != TAURUS_XPATH_NODESET) {
    fprintf(stderr, "Expected node set result\n");
    taurus_xpath_result_free(result);
    return;
}
```

## XPath Variables

For advanced usage, you can use XPath variables:

```c
TaurusXPathContext* ctx = taurus_xpath_context_new(doc);
taurus_xpath_context_set_variable(ctx, "min_price", "30.0");

TaurusXPathResult* result = taurus_xpath_eval_with_context(
    ctx, "//book[price > $min_price]");

taurus_xpath_context_free(ctx);
```

## Examples

### Extract All Book Titles

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "//book/title/text()", NULL);

if (taurus_xpath_result_type(result) == TAURUS_XPATH_NODESET) {
    int count = taurus_xpath_result_nodeset_count(result);
    for (int i = 0; i < count; i++) {
        TaurusNode* node = taurus_xpath_result_nodeset_item(result, i);
        if (taurus_node_type(node) == TAURUS_NODE_TEXT) {
            TaurusText text = (TaurusText)node;
            printf("Title: %s\n", taurus_text_value(text));
        }
    }
}
```

### Calculate Total Price

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "sum(//book/price)", NULL);

if (taurus_xpath_result_type(result) == TAURUS_XPATH_NUMBER) {
    double total = taurus_xpath_result_as_number(result);
    printf("Total price: %.2f\n", total);
}
```

### Count Elements

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "count(//book)", NULL);

if (taurus_xpath_result_type(result) == TAURUS_XPATH_NUMBER) {
    double count = taurus_xpath_result_as_number(result);
    printf("Found %.0f books\n", count);
}
```

### Check Existence

```c
TaurusXPathResult* result = taurus_xpath_eval(doc, "count(//book[@id='x']) > 0", NULL);

if (taurus_xpath_result_type(result) == TAURUS_XPATH_BOOLEAN) {
    int exists = taurus_xpath_result_as_boolean(result);
    printf("Book exists: %s\n", exists ? "yes" : "no");
}
```
