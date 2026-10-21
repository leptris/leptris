# Migrating from libxml2 to libleptris

libleptris is a clean-room C99 XML stack (XML 1.0, XPath 1.0, SAX,
DTD validation, XSLT 1.0, XInclude, HTML5) with no required runtime
dependencies. It is API-compatible in *shape*, not in spelling: the
same operations exist, with explicit status codes and
document-scoped ownership.

## The mental model

| libxml2 | libleptris |
|---|---|
| `xmlDocPtr` + `xmlNodePtr` | `LeptrisDocument` + `LeptrisElement` (opaque) |
| reference counting | none — everything is document-owned; free with `leptris_document_free` |
| `xmlFree(xmlChar*)` after `xmlDocDumpMemory` | `leptris_free_string` for caller-owned strings |
| global error handler / `xmlGetLastError` | a `LeptrisStatus` out-parameter on every call |
| thread-local state configured globally | every handle carries its context |

Rule of thumb: anything returned as `const char*` from a
`LeptrisDocument`-taking accessor is document-owned and lives until
`leptris_document_free`. Only `leptris_document_serialize` and
similar "out-string" calls hand you caller-owned memory.

## Parsing

```c
/* libxml2 */
xmlDocPtr doc = xmlReadMemory(buf, len, NULL, NULL, XML_PARSE_RECOVER);

/* libleptris */
LeptrisStatus st = LEPTIS_OK;
LeptrisDocument doc = leptris_parse_string(buf, len, &st);
if (!doc) { /* st carries the reason */ }
```

- Strict by default: malformed UTF-8 and well-formedness errors
  fail instead of silently recovering.
- `leptris_parse_string_flags(buf, len, LEPTRIS_PARSE_RECOVER, &st)`
  is the recover-mode analogue. `LEPTRIS_PARSE_COLLECT_HTML_ERRORS`
  adds WHATWG parse-error collection for HTML.
- HTML: `xmlReadMemory(..., HTML_PARSE_*)` →
  `leptris_parse_html_string` (WHATWG behaviour, 1555/1555 corpus
  parity).

## Reading the tree

| libxml2 | libleptris |
|---|---|
| `xmlDocGetRootElement(doc)` | `leptris_document_root(doc)` |
| `node->name` | `leptris_element_name(el)` |
| `node->children` loop | `leptris_element_first_child_any` + `leptris_element_next_sibling_any` |
| `xmlNodeGetContent(node)` | `leptris_element_get_text(el)` |
| `xmlGetProp(node, "id")` | `leptris_element_attribute_string(el, "id", NULL)` |
| `xmlStrdup(xmlGetProp(...))` | the returned string is document-owned — copy only if you must outlive the document |

## XPath 1.0

```c
/* libxml2 */
xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
xmlXPathObjectPtr r = xmlXPathEvalExpression(expr, ctx);

/* libleptris */
LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, expr);
size_t n = leptris_xpath_result_count(r);
leptris_xpath_result_free(r);
```

Compile once, evaluate many times:
`leptris_xpath_compile(expr)` → `leptris_xpath_compiled_eval(c, doc, ctx)`
→ `leptris_xpath_compiled_free(c)`. Namespaced queries take a
prefix→URI map through the `_ns` entry points. The W3C XPath 1.0
suite passes 438/438.

## Serialization

```c
/* libxml2 */
xmlChar *out; int size;
xmlDocDumpMemory(doc, &out, &size);
xmlFree(out);

/* libleptris */
char *out = leptris_document_serialize(doc, NULL);
if (out) leptris_free_string(out);
```

Text is escaped unconditionally on output (decoded text is data);
entity references parsed under `KEEP_ENTITY_REFS` remain
`ENTITY_REF` nodes and re-serialize as references.

## SAX / streaming

`xmlSAXUserParseMemory` → the `leptris_sax_*` API: register
start/end/text callbacks on a handler, then parse. For
pull-style streaming use the iterparse faces in the
bindings (`iterparse`, `iterparse_file`), which release prior
subtrees as the parse advances.

## Error handling

Every entry point writes a `LeptrisStatus` (`LEPTRIS_OK`,
`LEPTRIS_ERROR_*, LEPTRIS_RECOVERED`). Public entry points only ever
write public status constants; human-readable detail comes from
`leptris_document_last_error(doc)` after a failure.

## DTD validation

`xmlValidateDtd` → `leptris_dtd_*`: parse the internal/external
subset, then `leptris_dtd_validate_document`. EMPTY/ANY/Mixed/Element
models, `#REQUIRED`/`#IMPLIED`/`#FIXED` attributes and attribute type
validation are enforced.

## Things that differ on purpose

- **Limits are explicit.** Depth and size limits are configurable
  (`leptris_set_max_depth`) and enforced; there is no global mutable
  parser state.
- **No recover-by-default.** libxml2's permissive parse surprises
  (silent truncation) become explicit `LEPTRIS_PARSE_RECOVER` or
  HTML-mode choices.
- **Zero dependencies.** `utf8proc` and `iconv` are optional
  compile-time features; the core has no runtime deps.

## Nokogiri / REXML / lxml users

The language bindings carry compatible front ends: `leptris` for
Ruby (Nokogiri-shaped), Python (lxml-shaped), JavaScript
(WebAssembly), and Rust. The engine semantics above hold across all
four — that identity is enforced by the cross-runtime differential
corpus.
