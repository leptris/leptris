# REMAINING — the complete open-work ledger

SSOT for everything still open after v1.9.119 (2026-09-09).
Update entries as work ships; delete entries as they close.
Per-lane details live in the numbered docs; this file is the index
+ the graph.

## A. In flight

- **THE GRAPH TO SUCCESS (2026-09-12, after v1.9.138)** —
  three in-repo tracks + one user-side gate:
  1. **#659 HTML tail** (the only issue I own end-to-end):
     (a) misc insertion-mode slice — frameset-mode content drop
     (tests10:21/22), annotation-xml-without-encoding foreign
     tails (tests10:52-54), in-table clear-stack
     (adoption01:11) — then (b) the remaining corpus families
     in size order (template 29, script-data states ~60
     domjs/plain-text-unsafe, entities01 40, tests1/16 tails)
     while the floor only rises; (c) bindings expose
     `leptris_parse_html_string`/`html4` (leptris-ruby PR:
     `Leptris::HTML.parse` WHATWG + `parse4` Nokogiri-parity) →
     close #659.
  2. **QT3 growth (lanes 11/12)**: UCA collation → error-assertion
     runner (error channel) → assert-type/assert-xml/any-of →
     prod/ + Context* sets → lane-12 3.1 sets (array/, map/).
  3. **#682 last lever**: dispatch 2.25x vs the 2x bar (value-of
     135x, subtree-copy 2.27x already clear) — either one more
     dispatch lever or the user's scope call.
  4. **User-side gate (blocks #878 + lane 16/17 close)**: merge
     leptris-ruby#165 (→ closes #878 after standoc migration),
     #170 (Schematron), #171 (Diff), leptris-py#99, canon#191.
- **Schematron lane 16 COMPLETE on the C side** — phases 1-6 all
  on main through v1.9.129 (schema IR + SVRL, abstract patterns/
  let/phase, corpus 50/50, `leptris validate` CLI). The stale
  branch `feat/16-sch-phases34` carries already-merged SHAs —
  nothing pending there.

- **lane 17.4 diff/canon COMPLETE as PRs** (user merges):
  leptris-ruby#171 (Leptris::XML::Diff), lutaml/canon#191
  (structural-identity fast path). The canon integration caught
  the namespace-blind diff (PR #972, ships v1.9.129).
- **lane 16.6 binding PRs open** (user merges): leptris-ruby#170
  (Schematron), leptris-py#99 (Schematron). Migration prompt after
  they merge, then lane 16 CLOSES.
- **lane 16.5 Schematron corpus 50/50 COMPLETE** — branch
  `feat/16-sch-phase5`; PR + rebase-merge + v1.9.128, then phase
  6 (bindings Schematron::Schema + `leptris validate
  --schematron` + migration prompt). Implementation notes in
  16-schematron.md "Phase 5 COMPLETE".
- **leptris-ruby PR #165** — `Leptris::XML::RelaxNG` (user merges;
  requires libleptris >= 1.9.115, which is released). After merge:
  the metanorma-standoc migration off Jing (prompt on #878).

## B. Open GitHub issues (leptris/leptris)

- **#878** (nearly closed) — native RELAX NG validation. SHIPPED:
  pattern IR + validator + backtracking fold (v1.9.109-111), Jing
  conformance corpus gate 38/38 (v1.9.112), `<include>` via
  leptris_rng_parse_file (v1.9.113), `<param>` facets + portable
  pattern matcher (v1.9.114), schema errors on leptris_last_error
  (v1.9.115), Ruby binding (PR leptris-ruby#165). REMAINING: merge
  #165 → standoc migration off Jing (drafted on the issue) → close.
- **#682** (banked, USER SCOPE CALL pending) — in-process dispatch
  heavy 0.80x / light 0.76x vs lxml after the shipped levers
  (hash+mode-bucket dispatch, TLS consolidation, walk-first attr
  dup, AVT fast path, AST-cache TLS claim, streaming phase 1).
  Ceiling analysis (TODO 13): streaming remainder (~15%) + eval
  rewrites (~10%) + micro (~5-8%) tops out ~1.1x in-process —
  the >=2x bar is unreachable there while libxslt does identical
  work. Bars HELD: transform 5x, predicate 15.6x, xsltproc wall
  >=2x. Options on the issue; do not micro-grind awaiting the call.
- **#659** (the last open engine lane) — v1.9.116-119 jumps:
  DOCTYPE recording (WHATWG 295 -> 556, parity 372 -> 649),
  structural head/body + html>[head,body] ensuring in WHATWG mode
  (WHATWG -> 623, parity -> 777), template placement (652/783),
  MathML/SVG foreign content incl. integration points + breakout
  (755/783), in-table tbody/tr/colgroup synthesis (792/783). The
  comparator compares heads for real on the WHATWG meter
  (optional-empty-head is parity-only). Shipped cumulative:
  two-mode split, implied-head lift, foster parenting, simplified
  adoption agency + attr clones, doctype recording, structural
  head/body, entity perf (2.9x libxml2). REMAINING slices (fresh
  red-list, ranked): ruby rb/rt/rp (tests19), select-in-table
  residue, comment whitespace fidelity, entity edges, full
  AFE-list machinery.
- **#930** — CLOSED (v1.9.117): per-process pid-suffixed CLI temp
  paths.

## C. TODO.xslt-full lanes — residual items

- **01/03/04/06/08/09/10** — CLOSED.
- **05 (dates) tail**: value-level gaps, non-blocking:
  adjust-*-to-timezone (needs a timezone model), format-date/
  time/dateTime, current-dateTime/date/time.
- **07 (function items) tail**: fn:sort with key/collation arity,
  for-each-pair uneven inputs past the zip; verify whether the
  public typed-function-item surface landed with #683.
- **11 (XQuery core) tail**: qt3tests 1.0 subset adoption, Windows
  CLI harness for the XQuery driver.
- **12 (XQuery 3.1) tail**: qt3tests 3.1 subset adoption (language
  surface complete).
- **13 (perf)**: = #682 (banked). SAX binding-drain residual: the
  batched field-strip measured inconclusive; retry quiet-machine
  best-of-20, else engine-side batched-drain API (file when picked
  up). Registered lessons: benchmark fresh dirs + identical build
  type; quiet-window A/B only; CI is the gate.
- **14 (HTML)**: = #659 (the slices above).
- **15 (binding entries)**: leptris_xpath_eval_versioned (1.0-
  strict vs 3.x surface gating), result-type extensions from 07
  mirrored into both bindings. Gate: abi spec + mirrors drift
  check. NOTE: RelaxNG entries already shipped (leptris-ruby#165).
- **16 (Schematron 2025, NEW)**: the third validation pillar,
  native C — schema IR -> SVRL evaluator on our XPath engine ->
  exact-N conformance corpus -> bindings + CLI. Opened from the
  XML Prague 2026 gap map (section G). Doc: 16-schematron.md.
- **17 (XML diff, NEW 2026-09-09)**: native tree diff with the
  #869 digest prefilter + ordered edit script; consumer is the
  user's lutaml/canon gem (already rides leptris for C14N).
  Phases: op model -> children alignment -> serializer + CLI ->
  bindings + canon integration. Doc: 17-xml-diff.md.
- **USER-DIRECTED ORDER (2026-09-09)**: finish the XSLT/XPath/
  XQuery lane tails COMPLETELY, then Schematron (16), with XML
  diff (17) alongside. Concretely: 05 dates tail (current-*,
  implicit-timezone, adjust-*-to-timezone fixed-offset model,
  format-date/time/dateTime picture subset) -> 07 fn-items tail
  (fn:sort key/collation arity, for-each-pair past the zip) ->
  15 binding entries (leptris_xpath_eval_versioned + result-type
  mirrors) -> 16 + 17 in parallel -> 11/12 qt3tests subsets via
  the XTH connector.

## D. Bindings (user-owned repos — PRs only, never release)

- leptris-ruby#165 (RelaxNG) open, ready.
- moxml perf rows: parse 2.6-2.8x, serialize 2.4x, e2e 2.15x,
  xpath 31x, HTML 3.6-11x, C14N 23x AHEAD of Nokogiri; both 0.7x
  rows (bare reads, SAX drain) fixed via leptris-ruby#154/#156.

## E. External (Jing / metanorma-pdfa#98 follow-ups)

- sshaw/ruby-jing#6 upstream dormant; fork-under-metanorma decision
  is the user's (the strategic fix is #878 itself — now shipped).
- metanorma/standoc#1244 + mnconvert-ruby#40 — user review/merge.
- **Strategic: JVM-free validation is now REAL** — libleptris
  carries a Jing-parity RELAX NG validator since v1.9.115; the
  standoc migration is the remaining step (prompt on #878).

## F. Housekeeping

- ~20 stale local build* dirs (user's call; one-build-dir
  discipline going forward — Mac crash memory).
- Pre-existing warnings on main: html_parse.c:2160 (dual anonymous
  entity-table struct types, from #848) + sax/pull.c:447 (const
  discard in pull events). Both benign; fix in a cleanup PR.

## G. XML Prague 2026 gap map (2026-09-09) — where the field is going

Full inventory from all three day pages; lane 16 is the actionable
head. Ranked rest:

- **Schematron 2025** (Siegel + Users Meetup) -> LANE 16, above.
- **XPath/XSLT/XQuery 4.0** (Tovey-Walsh standards update; Kay's
  ordered-maps implementation paper; Leino XQuery-4 tutorial) —
  the long language lane; ordered maps are a data-structure
  design problem in our engine. Natural extension of lanes
  06-12; gate on XTH (below).
- **XTH connector** (Retter) — vendor-agnostic W3C qt3tests/
  xslttests runner, "small connector per processor", CI
  compliance reporting. This IS lanes 11/12 residual, done
  properly. Cheap, high credibility.
- **XML diff** (Quin/Brandes/Kutscherauer, Chawathe-style tree
  diff) — we hold #869 Merkle digest + C14N 23x; a native
  leptris diff is a medium lane, nothing in Nokogiri-land.
- **AI via XPath functions** (Bina; Nadolu) — "add a few XPath
  functions, LLMs across all XML tech". For us: an
  extension-function callback registration entry on the eval
  environment; bindings wire the LLM. Small hook, big demo.
- **ixml** (Pemberton state-of-play + case studies; Holman
  Crane-txt2xml) — parser-generator territory, conference-hot,
  no C implementation. Speculative; note only.
- **EPUB vertical** (EDRLab/Chomel) — epubcheck-shaped stack
  (zip+OPF+XHTML+RNG+Schematron) becomes buildable once lane 16
  lands. Application layer, later.
- **XProc 3** (Users Meetup; Hillman XProc-Baseline) and
  **XSL-FO** (Antenna House) — real but out of scope for now.

## Graph to success

```
leptris-ruby#165 ──user merge──> standoc migration off Jing (prompt on #878) ──> #878 CLOSED, pdfa#98 JVM-free
     │
#930 CLOSED (v1.9.117) ── per-process CLI temp paths shipped
     │
#659: corpus re-run ──> full AFE list + in-table modes + 23 tokenizer edges
      ──> WHATWG floor up, parity 372 held ──> bindings expose html entries ──> #659 CLOSED
     │
#682: USER CALL on scope (in-process ~1.1x ceiling vs wall-clock bars held)
      ──> close as-is OR one more streaming phase ──> lane 13 closed
     │
lane 16 Schematron (after #659 settles): IR ──> SVRL evaluator ──> exact-N corpus
      ──> bindings + CLI validate ──> migration prompt ──> third pillar DONE
     │
05/07/11/12 tails + XTH connector (qt3tests done properly) + lane 15 versioned entries
      ──> bindings wave
     │
F: warnings cleanup + build-dir consolidation (user call)
```

XSLT speed vs the field (current, gated): transform 5x, predicate
15.6x, xsltproc wall >=2x, HTML parse 2.9x, C14N 23x binding-armed
— every USER-VISIBLE feature is ahead; the only sub-parity row is
in-process dispatch-heavy (0.80x, ceiling-analyzed, awaiting the
user's scope call on #682).
