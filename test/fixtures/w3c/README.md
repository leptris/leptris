# W3C XPath 1.0 Conformance Tests

This directory contains W3C XPath 1.0 conformance test data extracted from pugixml's test suite.

## Source

- **Specification**: XPath 1.0 - W3C Recommendation
- **URL**: https://www.w3.org/TR/xpath-10/
- **Test Data Source**: pugixml test suite (which includes W3C conformance tests)
- **License**: W3C Document License

## Purpose

These tests ensure Taurus's XPath implementation conforms to the W3C XPath 1.0 specification.

## Test Categories

### Path Expression Tests
- Abbreviated syntax (`//`, `@`, `.`, `..`)
- Full syntax (`child::`, `descendant::`, etc.)
- Predicates (`[1]`, `[last()]`, `[@attr]`)
- Node tests (`*`, `node()`, `text()`, etc.)

### Function Tests
- Node-set functions: `last()`, `position()`, `count()`, `id()`, `local-name()`, `namespace-uri()`, `name()`
- String functions: `string()`, `concat()`, `starts-with()`, `contains()`, `substring-before()`, `substring-after()`, `substring()`, `string-length()`, `normalize-space()`, `translate()`
- Boolean functions: `boolean()`, `not()`, `true()`, `false()`, `lang()`
- Number functions: `number()`, `sum()`, `floor()`, `ceiling()`, `round()`

### Operator Tests
- Comparison: `=`, `!=`, `<`, `<=`, `>`, `>=`
- Boolean: `and`, `or`
- Arithmetic: `+`, `-`, `*`, `div`, `mod`
- Union: `|`

## References

- XPath 1.0 Spec: https://www.w3.org/TR/xpath-10/
- XPath 1.0 Test Suite: https://www.w3.org/XML/Query/test-suite/