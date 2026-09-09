# 16 — Schematron (2025 edition), native in libleptris

Third validation pillar: DTD (grammar) + RELAX NG (structure) +
Schematron (rules). Opened from the XML Prague 2026 gap map
(2026-09-09); Siegel's "Schematron 2025 – Technology Update" talk
+ the standing Schematron Users Meetup confirm the ecosystem just
moved (new edition published Sept 2025) while OSS tooling largely
hasn't.

## Why INSIDE libleptris (not separate software)

- Schematron evaluation is ~95% XPath evaluation. Our engine
  (438/438 W3C + libxslt suite 205/205 behind it) is the moat; a
  separate binary would FFI back into us anyway — circular.
- Bindings get `Leptris::XML::Schematron` free, mirroring
  `Leptris::XML::RelaxNG` (leptris-ruby#165). Nokogiri has NO
  native Schematron today (users shell out or run XSLT pipelines)
  — this is the binding differentiator after RNG.
- CLI `leptris validate` completes as a one-stop validator.
- Performance: isoschematron/lxml is a 2-pass XSLT pipeline on
  libxslt; a native IR walker holds >=2x trivially given transform
  5x / predicate 15.6x. schxslt-on-Saxon (JVM) is the wall-clock
  reference for realistic documents.

## Phases (mirror the RNG arc, #878)

1. **Schema model IR** — parse `sch:` schema/pattern/rule/assert/
   report/@context/@test/@role/@id, `ns`, `let`, `phase`,
   `diagnostics`, `properties`, `title`, `param`. queryBinding=xslt
   (subset mapped to our XPath 1.0+extensions surface; loud-refuse
   unknown bindings). Errors on leptris_last_error, same contract
   as RNG.
2. **Evaluator + SVRL emitter** — per pattern: select context
   nodes, evaluate asserts in document order, emit
   `svrl:schematron-output` (active-pattern, fired-rule,
   failed-assert, successful-report with @location/@test).
   Validity = zero failed-asserts. Context-node loop reuses the
   batch-context eval path (#560).
3. **Abstract patterns + params, let scoping** (schema -> pattern
   -> rule chains), global variables, `document()` access via the
   resolver already behind RNG `<include>`.
4. **Phases, diagnostics, properties** + rule/pattern @role
   filtering.
5. **Conformance corpus** — vendor schematron-tests / schxslt
   corpus, gate EXACT like Jing 38/38 (agreement table +
   provenance JSON). 2025-edition features land behind the same
   gate: refuse loud until implemented.
6. **Bindings + CLI** — leptris-ruby `Schematron::Schema`
   (parse/parse_file/#valid?/#validate -> SVRL or error list),
   python mirror, `leptris validate --schematron`. Then the
   migration prompt (standoc/oXygen world) like #878's.

## Bars

- isoschematron/lxml pipeline >=2x (primary).
- schxslt on Saxon-JVM wall-clock >=2x on realistic rule sets.
- Corpus gate exact-N (no partial credit), CI leg included.

## Out of scope

XSLT2/3 query binding beyond our engine surface; XProc steps;
epubcheck-class verticals (application layer, build on top later).

## Related (XML Prague 2026 gap map, not this lane)

XPath/XSLT/XQuery 4.0 ordered maps (Kay); XTH connector (Retter)
= lanes 11/12 harness; XML diff (Quin et al.) on #869 Merkle
digest; ixml (Pemberton) — speculative; AI-via-XPath-functions
(Bina/Nadolu) — needs only an extension-fn callback entry; EPUB
vertical (EDRLab) after Schematron; XProc 3 / XSL-FO — not now.
