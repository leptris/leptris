# TODO.xpath2 — XSLT 2.0/3.0, XPath 3.1, and XQuery (from Saxon-HE)

The engine for the 1.x line is XSLT 1.0 (TODO.transform) on the
XPath 1.0 layer. This board plans the 2.0/3.0-generation surface
by PORTING (not inventing) the semantics from Saxon-HE, the
canonical open-source reference implementation:

    Source of truth:  git@github.com:Saxonica/Saxon-HE.git
    Local mirror:     ~/src/external/Saxon-HE/13/source/saxon13-0source.zip
                      (Java sources under net/sf/saxon/)

Porting discipline (same as TODO.transform):
- READ the Java source for semantics; implement in C99 against the
  existing leptris architecture (pool allocator, compile-once
  instruction forests, function-pointer dispatch).
- No Java runtime. No GC assumptions. Every data structure maps to
  an existing leptris primitive (StringView, pool-owned nodes,
  nodeset arrays) or a new one with an explicit lifecycle.
- Conformance specs come from the W3C test suites (XSLT 2.0, XPath
  2.0/3.1, XQuery 3.1) — ported as falsifiable per-feature cases.

## Sequencing (each phase = one PR, full specs)

### 01-xpath31-type-system
- Sequences (item()+ semantics) as first-class results — replace
  the 1.0 nodeset/number/string/boolean four-type result model
  with a sequence-of-items model that degrades cleanly to 1.0.
- Atomic types: xs:integer, decimal, double, float, string,
  boolean, date/time family, duration, QName, anyURI, untyped.
- Static typing pass (optional diagnostics; dynamic evaluation is
  normative).
- Reference: net/sf/saxon/type/, net/sf/saxon/value/.

### 02-xpath31-expressions
- `for`/`let` expressions, inline functions, arrow (`=>`) and
  `!` operators, `if/then/else`, quantified expressions.
- Higher-order function calls, dynamic function references.
- Maps, arrays, lookup (`?key`, `?index`).
- String templates, `parse-ietf-date`, `format-integer/number/date`.
- Regex: XPath 3.1 functions (`matches`, `replace`, ` tokenize`,
  `analyze-string`) — replaces the POSIX ERE subset.
- Reference: net/sf/saxon/expr/, net/sf/saxon/functions/.

### 03-schema-aware-lite
- `validate` expressions need XSD. Porting full XSD 1.1 is its own
  board; the interim step is PSVI-lite: typed element/attribute
  annotations driven by a simple schema loader. (Full XSD tracked
  separately in TODO.validate.)

### 04-xslt20-30
- Backwards-compatible + 2.0 modes; xsl:function, xsl:sequence,
  tunnel parameters, temporary trees, grouping (xsl:for-each-group
  — 4 grouping kinds), regex grouping, xsl:analyze-string,
  xsl:try/catch, xsl:evaluate, xsl:iterate, xsl:next-iteration,
  xsl:accumulator, xsl:mode declarations, package (3.0) semantics.
- Reference: net/sf/saxon/style/, net/sf/saxon/expr/instruct/.

### 05-xquery31
- Query prolog (declare variable/function/namespace/collation),
  FLWOR (for/let/where/order by/group by/return/window),
  modules and imports, update-free XQuery 3.1 + Update Facility 1.0
  as a later phase.
- Reference: net/sf/saxon/query/.

### 06-conformance-suites
- Port the W3C XSLT/XPath/XQuery 3.1 test-suite runner cases as
  in-tree specs (same layout as test/xpath/test_xpath_conformance).

## Explicit non-goals (tracked elsewhere)

- Streaming (XSLT 3.0 streaming transformations) — separate board
  after 04 (TODO.stream).
- Schema-aware processing requiring full XSD 1.1 — TODO.validate.

## Pre-conditions

- TODO.transform fully closed (XSLT 1.0 bar cleared; §12 bridge
  install landed) — this board builds on that engine, not around it.
- The XPath 1.0 evaluator keeps its 438/438 conformance; 3.1 modes
  activate per-expression-version, never globally.
