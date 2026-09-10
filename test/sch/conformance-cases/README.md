# Schematron conformance corpus (lane 16 phase 5)

50 self-contained testcases vendored verbatim from
[schematron/schematron-conformance](https://github.com/schematron/schematron-conformance)
(`src/main/resources/tests/{core,svrl}`, MIT-licensed upstream).
Each `<testcase>` embeds a primary document, a `sch:schema`, and a
verdict in `@expect` (`valid` / `invalid` / `error` / `ambiguous`);
the `svrl-*` cases additionally carry `<expectations>` XPath checks
over the SVRL output.

Regenerate by re-downloading the `tests/` tree from upstream and
re-emitting the `kCases` table in `../test_schematron_corpus.cpp`
from each file's `@expect`.

Upstream commit at vendor time: HEAD of default branch, 2026-09-10.
