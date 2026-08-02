# TODO 47: XPath evaluator leak audit

**Priority**: P1 (correctness — leaks on XPath-heavy workloads)
**Status**: Planned
**Effort**: M

## Problem

`src/taurus/xpath/evaluator.c` (700+ lines) has its own
allocation paths that haven't been audited:

- `xpath_context_new` allocates the XPathContext
- `xpath_context_register_namespace` allocates prefix/URI strings
- Various AST allocations during parsing
- Result objects (`xpath_result_new`)

After TODO 34 fixed the `lang()` function's leaks, other evaluator
paths may still leak.  The XPath test specs pass but don't probe
leak paths deeply.

## Root cause

The evaluator predates the pool-ownership model.  It uses calloc for
its own state and assumes the caller will free.  But the caller (the
public `taurus_xpath_eval`) does free via `xpath_context_free` /
`xpath_result_free` — most of the time.  Edge cases (early return on
error, partial evaluation) may skip the free.

## Fix

1. **Audit every allocation** in evaluator.c.  Confirm each has a
   matching free on every exit path.

2. **Where possible, route through the document pool.**  Context
   allocations (XPathContext struct, namespace mappings) are
   document-scoped — they should come from `document->pool` so
   `taurus_document_free` releases them if the user forgets to call
   `xpath_context_free`.

3. **Result objects stay calloc'd** — they're returned to the caller
   and freed explicitly via `taurus_xpath_result_free`.  That's the
   right model.

## Tests

`test/xpath/test_xpath.cpp` extends with specs that exercise every
XPath feature:

```cpp
TEST(XPathLeaks, ComplexQueryDoesNotLeak) {
    const char xml[] = "<r><a x='1'><b>text</b></a><a x='2'><b/></a></r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Exercise every XPath feature: axes, functions, predicates, union. */
    const char* queries[] = {
        "//a", "//a[@x='1']", "//a[b]",
        "count(//a)", "string(//a/b)", "//a[1]/b | //a[2]/b",
        "name(//a[1])", "namespace-uri(//a)",
        "//a[parent::r]", "//a[descendant::b]",
        "//a[position() > 1]",
        "//*[contains(string(.), 'text')]",
    };

    for (const char* q : queries) {
        TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, q);
        if (r) taurus_xpath_result_free(r);
    }

    taurus_document_free(doc);
    // Under leaks --atExit --: 0 bytes leaked.
}
```

## Architecture notes

The XPath evaluator is a separate "minilanguage" — it parses its own
syntax, evaluates its own AST.  Its allocation model should match the
rest of the library: document-scoped for state, explicit-free for
results.  Currently it's a mix.

## Verification

```bash
leaks --atExit -- build/test/test_xpath
# Expected: 0 bytes leaked after the ComplexQueryDoesNotLeak spec.
```
