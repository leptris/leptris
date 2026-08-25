# Leptris → Saxon-HE Parity Roadmap

**Goal:** full feature parity with the latest Saxon-HE (13.x) — the
open-source edition of the reference XSLT/XPath/XQuery processor.

**What "parity with Saxon-HE" means (and doesn't):** Saxon-HE ships
XSLT 3.0 (non-streaming), XPath 3.1 (+ select 4.0 extensions in 13),
XQuery 3.1/4.0, and a non-schema-aware processor. Schema-aware
processing and streaming are **PE/EE-only** — parity therefore
excludes them (our full XSD 1.1 validator lives on TODO.validate;
streaming on TODO.stream).

**References:**
- Source: `~/src/external/Saxon-HE/13` (Java sources,
  `net/sf/saxon/{expr,om,style,query,type,functions,...}`).
- Test suites: `~/src/external/saxon-xslt30tests` (8,486 stylesheets,
  catalog-driven) + Saxonica/qt3tests (to clone; XPath/XQuery 3.1).
- Behavioral reference for the 1.x line: libxslt
  (`~/src/external/libxslt`).

**Current position (2026-08-25):** libleptris 1.9.0-dev on main:
XML 1.0 (W3C-conformant parse), XPath 1.0 (438/438 W3C suite),
XSLT 1.0 (feature-complete per §1–§16; the adopted libxslt suite is
the active conformance gate — 48/205 cases passing, 157 tracked in
`test/xslt/open_cases.txt`; **no XSLT release ships until 205/205**).

---

## Version plan

Each version = one theme, shipped only when its suite gate passes.
Durations assume the current cadence (engine work + suites + release).

### v1.10.0 — XSLT 1.0 conformance (libxslt gate)
**Theme:** close every open libxslt-suite case. **Gate:** 205/205 +
45/45 XsltFull + full suite green.
- Remaining clusters (by size, from `test/xslt/open_cases.txt`):
  - 20+ cases: `xsl:number` `from`/`level=any` edge semantics,
    `format-number` pattern corners (bug-25-, bug-63.., bug-90..)
  - 15+ cases: attribute-set merge/import precedence (bug-93-102
    family), `xsl:attribute` namespace AVT
  - 10+ cases: `document()` multi-file chains, `xsl:strip-space` on
    included sheets, output indent="yes"
  - 8 cases:  `key()` cross-document, `generate-id()` stability
  - 3 crash cases (bug-111/130/147): recursion-depth accounting in
    `xsl:apply-templates` chains
- Exit criteria: open list empty, ASAN/TSAN/Windows CI green.

### v1.11.0 — Engine hardening + EXSLT completion
**Theme:** everything around XSLT 1.0 that processors actually ship.
- EXSLT full: `dyn:*` (dynamic evaluation), `exsl:document`,
  `saxon:*` compatibility subset, `str:tokenize` corners, crypto:NO.
- `xsl:output` `indent="yes"` pretty-printer for XML + HTML
  (libxslt-compatible modes).
- Large-document performance pass: template-selection memoization,
  key index sharing across transforms, RTF pool reuse.
- Gate: libxslt suite stays 205/205; EXSLT suite
  (`~/src/external/libxslt/tests/exslt`) adopted as specs (all of
  common/functions/math/sets/strings/date).

### v2.0.0 — XPath 2.0/3.1 type system + sequences
**Theme:** the data model rewrite everything else builds on.
(Saxon: `net/sf/saxon/om`, `net/sf/saxon/value`, `net/sf/saxon/type`.)
- Sequence model: every expression yields `item()*` — replaces the
  1.0 four-type result; nodesets are sequences of items.
- Atomic types: `xs:integer/decimal/double/float/string/boolean/
  anyURI/QName/Name/NCName/date/time/dateTime/duration/
  dayTimeDuration/yearMonthDuration/base64Binary/hexBinary/
  untypedAtomic`, with typed value spaces + subtype relations.
- `castable as` / `cast as` / `instance of` / `treat as`, constructor
  functions, `xs:*` validation-lite (non-schema-aware = untyped ok).
- Backwards mode: 1.0 expressions keep exact 1.0 semantics (the
  versioned-expression switch; 438/438 suite must stay green).
- Gate: qt3tests XPath-cast/constructor/sequence subset adopted;
  1.0 suites green.

### v2.1.0 — XPath 2.0 expressions
**Theme:** the 2.0 expression surface (Saxon: `net/sf/saxon/expr`).
- `for $x in ... return`, `if/then/else`, `some/every ... satisfies`,
  quantified expressions, `to` ranges, sequence ops (`union`,
  `intersect`, `except` on any sequences), `instance of` tests.
- Comparisons: value vs general (`eq`, `lt`, `>`, `is`), node
  identity, `deep-equal`, collations.
- Functions 2.0+: `matches/replace/tokenize` (Unicode-aware regex —
  replaces POSIX), `avg/min/max/sum/distinct-values`,
  `substring-before/after` with collation, `format-date/time`,
  `adjust-timezone`, `resolve-uri`, `encode-for-uri`, `QName()`,
  `local-name-from-QName`, `node-name`, `zero-or-one/one-or-more`,
  `exactly-one`, `subsequence`, `insert-before`, `remove`, `index-of`,
  `reverse`, `unordered`, `deep-equal`, `root`, `idref`.
- Maps + arrays (3.1 preview since they share the value model):
  map constructors `{...}`, lookup `?key`, array `[...]`, `?number`.
- Gate: qt3tests expression subset; 1.0 suites green.

### v2.2.0 — XPath 3.0/3.1 + dynamic function machinery
- Inline functions `function($x){...}`, arrow `=>`, `!` mapping,
  named function references `name#arity`, higher-order calls,
  `function-lookup`, `function-name`, `function-arity`, partial
  application `?`.
- String templates, `parse-xml`/`parse-xml-fragment`,
  `analyze-string`, `format-integer` full, `xml-to-json`/
  `json-to-xml`, `load-xquery-module` (stub), `collation-key`,
  `available-environment-variables`, `environment-variable`.
- 4.0 previews behind a flag (Saxon 13 defaults much of 4.0):
  `otherwise`, `||` string concat, `fn:charset`… tracked as
  Saxon-13 parity items, not spec-3.1 ones.
- Gate: qt3tests full XPath 3.1 subset adopted + green.

### v3.0.0 — XSLT 2.0
**Theme:** the 2.0 instruction surface (Saxon: `net/sf/saxon/style`).
- Backwards-compatible + 2.0 strict modes; `xsl:stylesheet
  version="2.0"` switches sequence semantics.
- `xsl:function` (user functions, typed signatures),
  `xsl:sequence` (no-copy node construction), tunnel parameters,
  `xsl:for-each-group` (4 grouping kinds), `xsl:analyze-string`
  (+ matching/non-matching), `xsl:result-document` (multiple
  outputs), `xsl:next-match`, temporary trees, typed
  `xsl:variable as="..."`, regex groups `$1..$n`, `xsl:perform-
  with-accumulators` (later), pattern enhancements (predicates on
  any step), `xsl:value-of select="..." separator`.
- Schema-free typed access (document-less PSVI-lite).
- Gate: xslt30tests version="2.0" subset (the suite's 2.0 test
  sets) adopted + green.

### v3.1.0 — XSLT 3.0 (non-streaming)
- `xsl:evaluate` (dynamic expression eval), `xsl:iterate` +
  `xsl:next-iteration`/`xsl:break`, `xsl:try`/`xsl:catch` (error
  codes + diagnostics), `xsl:accumulator`/`xsl:accumulator-rule`,
  `xsl:mode` declarations (on-no-match/on-deep-match),
  shadow attributes `_name="{...}"`, `xsl:where-populated`,
  `xsl:on-empty`, `xsl:on-non-empty`, text value templates
  (`{...}` in text nodes with expand-text), `xsl:global-context-
  item`, packages (`xsl:use-package` — non-streaming), `xsl:assert`.
- Map/array construction + navigation as first-class results.
- Gate: xslt30tests non-streaming subset green (target ≥ Saxon's
  own HE pass rate on the non-streaming sets).

### v3.2.0 — XQuery 3.1/4.0
(Saxon: `net/sf/saxon/query`.)
- Prolog: `declare variable/function/namespace/collation/
  base-uri/construction/ordering/default collation`, module
  imports + library modules, `declare context item`.
- FLWOR: `for/let/where/group by/order by/count/return/window`,
  `allowing empty`, `count` clauses, tumbling/sliding windows.
- 3.1 built-ins: map/array functions, `transform()` (XSLT-from-
  XQuery), `fn:load-xquery-module`, higher-order library.
- Gate: qt3tests XQuery subset + qt3extra adopted + green.

### v3.3.0 — Parity polish (Saxon-HE 13 feature checklist)
- 4.0 extensions Saxon 13 ships by default (Saxonica spec status):
  assignment expressions, `fn:items-at`, string concat `||`,
  `otherwise`, records (limited), `fn:foot`/`fn:hd`/`fn:ticks`,
  coercion rules, keyword arguments in calls.
- APIs Saxon-HE parity: compile-to-bytecode is internal (our VM
  already is); `Configuration`-level toggles mapped to leptris
  options; CLI `xslt`/`xquery`/`gizmo`-equivalent entry points.
- Serialization: full `fn:serialize` params (indent, line-separator,
  suppress-indentation, byte-order-mark, normalization-form,
  item-separator, json-node-output-method, html-version).
- Gate: side-by-side Saxon-HE behavior audit on a sampled corpus
  (same outputs for same inputs on the adopted suites).

---

## Suite gates (the definition of done per version)

| Version | Suite | Where |
|---|---|---|
| 1.10 | libxslt general (205) + XsltFull (45) | in-tree |
| 1.11 | libxslt EXSLT | `~/src/external/libxslt/tests/exslt` |
| 2.0–2.2 | qt3tests (XPath 2.0→3.1 subsets) | Saxonica/qt3tests (clone) |
| 3.0 | xslt30tests (2.0 test sets) | `~/src/external/saxon-xslt30tests` |
| 3.1 | xslt30tests (non-streaming 3.0 sets) | same |
| 3.2 | qt3tests XQuery + qt3extra | clone |
| 3.3 | Saxon-HE side-by-side corpus audit | generated |

## Non-goals (explicitly out of parity scope)

- **Streaming** (XSLT 3.0 streaming transformations) — Saxon EE;
  tracked on TODO.stream.
- **Schema-aware processing** (XSD validation feeding the type
  system) — Saxon PE/EE; tracked on TODO.validate. The 2.0+ type
  system lands schema-free (untyped/psvi-lite), exactly like HE.
- **XSLT 4.0 / XPath 4.0 full** — Saxon 13 ships previews; parity
  targets what Saxon-HE 13 enables BY DEFAULT.

## Standing rules

1. **No release until its gate passes** (user directive, 2026-08-25).
2. Every adopted suite's open list must shrink monotonically; a
   reopened case is a release blocker.
3. Port semantics from the Saxon sources; never invent. Where
   libxslt and Saxon disagree on 1.0, libxslt is the reference (we
   adopted its suite first).
