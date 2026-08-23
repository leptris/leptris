# 06 — First-party EXSLT-style extension pack

User report: `leptris_xpath_register_function` exists precisely for
this — ship one C implementation of the commonly-missed XPath 2.0
conveniences instead of N binding reimplementations.

Public API: `leptris_exslt_enable(doc)` — registers the pack for that
document (scoped, freed with the doc; prefixed names can never clash
with the XPath 1.0 core). Native handlers (full result types), not
the string-only custom-fn bridge:

- str: replace, tokenize, split, concat, padding
- set: distinct, intersection, difference, leading, trailing
- math: max, min, abs, sqrt, power

Scope notes: str:replace takes string args (EXSLT's parallel-nodeset
form is out of scope v1); tokenize/split build nodesets of synthetic
text nodes (XPathNodeSet grows an owns_synthetic_text flag to free
them, mirroring owns_attributes).

DONE 2026-08-23: 14 handlers + enable API + specs for every function
incl. kind checks on tokenize output; registry lookup by prefixed
name (parser stores "str:replace" for QName tokens).
