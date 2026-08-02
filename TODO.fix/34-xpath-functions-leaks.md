# TODO 34: XPath `functions.c` leak audit

**Priority**: P1 (correctness — leaks surface on XPath-heavy workloads)
**Status**: Planned
**Effort**: M

## Problem

`src/taurus/xpath/functions.c` calls `taurus_sv_to_cstr` at several
sites without freeing the intermediate:

```
1699:  ns_uri    = taurus_sv_to_cstr(&attr->namespace_uri_view);
1705:  attr_name = taurus_sv_to_cstr(&attr->name_view);
1731:  prefix    = taurus_sv_to_cstr(&attr->prefix_view);
1748:  lang_attr = taurus_sv_to_cstr(&attr->value_view);
```

These fire when XPath functions like `local-name()`, `namespace-uri()`,
`name()`, `lang()` traverse attributes.  Each call allocates a 16-byte
buffer that's never freed.

## Root cause

The XPath evaluator was written before the pool-ownership model (TODOs
05/15/25) was in place.  String conversions use raw calloc because the
evaluator didn't have a pool handle to pass.

The XPath context (XPathContext in evaluator.c) DOES have access to
the document via `context->document`, and therefore to
`document->pool`.  The functions just don't use it.

## Fix

For each of the 4 sites:

1. Get the pool: `TaurusMemoryPool* pool = context->document->pool;`
2. Replace `taurus_sv_to_cstr(sv)` with
   `taurus_sv_to_cstr_pooled(sv, pool)`.

If the result is consumed locally (e.g., a one-shot comparison), free
it after use.  If it's stored on the result, pool-route so the
document free releases it.

## Tests

`test/xpath/test_xpath.cpp` extends with specs that exercise
namespace-aware queries:

```cpp
TEST(XPathNamespaceFunctions, LocalNameOnNamespacedAttr) {
    const char xml[] = "<r xmlns:ns='http://x' ns:attr='v'/>";
    /* ... evaluate local-name(//ns:attr), verify, free, no leak ... */
}

TEST(XPathNamespaceFunctions, NamespaceUriOnAttr) { /* ... */ }
TEST(XPathNamespaceFunctions, NameOnNamespacedElement) { /* ... */ }
TEST(XPathLangFunction, LangPredicate) { /* ... */ }
```

CI runs each under `leaks --atExit --`; zero bytes leaked.

## Architecture notes

The fix threads the pool through XPath evaluation contexts.  After
this, the pool-ownership invariant (every string reachable from a
document comes from the document's pool) holds for XPath results too.

## Verification

```bash
build/test/test_xpath
# All XPath specs pass; under leaks, zero bytes leaked.
```
