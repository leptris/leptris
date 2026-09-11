# W3C QT3 test suite subset (lanes 11/12)

Vendored verbatim from [w3c/qt3tests](https://github.com/w3c/qt3tests)
(W3C Software and Document Notice). Batches: `fn/substring`
(48 test-cases, 45 adopted) with its `concepts` source document;
`fn/contains` (48, 46); `fn/starts-with` (64, 43);
`fn/ends-with` (55, 34); `fn/concat` (96, 80). UCA-collation
args, error-assertion cases, and concat's canonical
xs:double/xs:float E-notation cases (the formatter keeps
libxml2 parity) wait on future slices.

The runner (`../test_qt3.cpp`) parses each test-set with
libleptris itself, adopts every test-case whose assertions and
environment are within the supported surface, evaluates the
embedded query through the public XQuery API, and gates exact:
`agree == run == <adopted count>`.

Supported assertions: `assert-string-value`, `assert-eq`,
`assert-true`, `assert-false`, `all-of` wrappers. Cases using
`assert-type` or unsupported environments are excluded and
counted in the runner's comments — the adopted count rises as
the runner grows (the XTH connector shape).

Regenerate by re-fetching the same paths from upstream HEAD and
re-counting the adopted set in the runner.

Upstream commit at vendor time: HEAD of default branch,
2026-09-10.
