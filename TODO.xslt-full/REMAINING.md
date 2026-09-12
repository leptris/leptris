# REMAINING — the complete open-work ledger

SSOT for everything still open after v1.9.144 (2026-09-12).
Update entries as work ships; delete entries as they close.
Per-lane details live in the numbered docs; this file is the index
+ the graph.

## A. In flight — the staged path (2026-09-12)

**STAGE 0 (shipping now)** — template fence slice on
feat/html-template: WHATWG-gated in-template end-tag fence +
structural html/head/body drop (attrs NOT merged) + template
marker-clear in the AAFE list (corpus 1169 → 1175, parity 784
held). The v1/v2 content-level heuristics were built, measured
net-negative, and REVERTED — the remaining ~25 template reds need
the real mode stack (14-html-mode.md entry (h) banked,
entry (i) shipped; the mode-stack requirement lives there).

**STAGE 1 — HTML tail (#659), the only engine lane owned
end-to-end:**
1.1 template insertion-mode stack: each open template saves its
    insertion mode; starts switch it (tr → in_row, td → in_cell,
    thead/tbody/caption/col → in_table), end tags restore; clears
    the ~25 template.dat reds (floor → ~1200)
1.2 the 3-red walker mystery (plain-text-unsafe 12/13,
    noscript01:1) — engine output is correct, the corpus walker
    compares the comment as ''
1.3 remaining corpus families in size order (tests1/16 tails)
1.4 bindings expose leptris_parse_html_string/html4
    (leptris-ruby PR: Leptris::HTML.parse WHATWG + parse4
    Nokogiri-parity) → CLOSE #659

**STAGE 2 — XSLT/XPath/XQuery completion (lanes 05/07/11/12/15):**
2.1 QT3 growth: UCA collation → error-assertion channel →
    assert-type/assert-xml/any-of → prod/ + Context* sets → 3.1
    sets (array/, map/)
2.2 lane 05 dates tail: current-*, implicit-timezone,
    adjust-*-to-timezone (fixed-offset model), format-date/
    time/dateTime picture subset
2.3 lane 07 fn-items tail: fn:sort key/collation arity,
    for-each-pair past the zip
2.4 lane 15: leptris_xpath_eval_versioned (1.0-strict vs 3.x
    surface gating) + result-type mirrors in both bindings
    (gate: abi spec + mirror-drift check)

**STAGE 3 — Schematron lane 16: COMPLETE on the C side** (schema
IR + SVRL evaluator + corpus 50/50 + `leptris validate`,
v1.9.128/129). CLOSES when the user merges leptris-ruby#170 +
leptris-py#99.

**STAGE 4 — user-side gate (blocks #878 + lane 16/17 close):**
merge leptris-ruby#165 (RelaxNG → then the standoc migration off
Jing, prompt on #878 → #878 CLOSED), #170 (Schematron), #171
(Diff), leptris-py#99 (Schematron), lutaml/canon#191 (structural
fast path). All built, tested, waiting on the merge button.

**STAGE 5 — #682 scope call (the only open decision):**
(a) one more streaming phase (~+15%, lands ~1.1x in-process),
(b) rescope the dispatch bar to >=1x in-process + >=2x xsltproc
    wall (already true), (c) close as-is. Every user-visible row
    is already >=2x. Do NOT micro-grind awaiting the call.

**STAGE 6 — website benchmark-system v2:** owned by the website
agent (prompt delivered 2026-09-12): XML/XSLT/YAML (@yeptris) x
ruby/python/c scoreboard. We feed measured numbers only.

## B. Open GitHub issues (leptris/leptris)

- **#878** (nearly closed) — native RELAX NG validation. SHIPPED:
  pattern IR + validator + backtracking fold (v1.9.109-111), Jing
  conformance corpus gate 38/38 (v1.9.112), `<include>` via
  leptris_rng_parse_file (v1.9.113), `<param>` facets (v1.9.114),
  schema errors on leptris_last_error (v1.9.115), Ruby binding
  (PR leptris-ruby#165). REMAINING: merge #165 → standoc
  migration off Jing (drafted on the issue) → close.
- **#682** (banked, USER SCOPE CALL pending) — in-process dispatch
  heavy 0.80x / light 0.76x vs lxml after the shipped levers
  (hash+mode-bucket dispatch, TLS consolidation, walk-first attr
  dup, AVT fast path, AST-cache TLS claim, streaming phase 1).
  Ceiling analysis (TODO 13): streaming remainder (~15%) + eval
  rewrites (~10%) + micro (~5-8%) tops out ~1.1x in-process —
  the >=2x bar is unreachable there while libxslt does identical
  work. Bars HELD: transform 5x, predicate 15.6x, value-of 135x,
  subtree-copy 2.27x, xsltproc wall >=2x. Options on the issue.
- **#659** — v1.9.116-119 jumps, then the v1.9.137-144 WHATWG
  arc: full AFE list + foreign scope boundaries (933 → 1027),
  in-table clear-stack + frameset (→ 1038), bogus-markup edges
  (→ 1046), script-data escaped states (→ 1096), RCDATA family
  (→ 1129), numeric-reference end states — entities01 100% green
  (→ 1169), template fence subset (→ 1175 of 1753). Parity 784
  held throughout; html4 mode stays byte-pinned. REMAINING =
  Stage 1 above. (Also shipped: leptris_element_expanded_name,
  v1.9.144, the adapter batched-accessor primitive.)
- **#930** — CLOSED (v1.9.117).

## C. TODO.xslt-full lanes — residual items

- 01/03/04/06/08/09/10 — CLOSED.
- 05 (dates) tail → Stage 2.2. 07 (fn items) tail → Stage 2.3.
- 11/12 (XQuery) tails → Stage 2.1 QT3 subsets via the XTH
  connector shape.
- 13 (perf) = #682 (Stage 5). SAX binding-drain residual: retry
  quiet-machine best-of-20, else engine-side batched-drain API.
- 14 (HTML) = #659 (Stage 1).
- 15 (binding entries) → Stage 2.4. RelaxNG entries already
  shipped (leptris-ruby#165).
- 16 (Schematron 2025): COMPLETE C-side = Stage 3; binding PRs
  open (user merges).
- 17 (XML diff): PRs open = Stage 4 (leptris-ruby#171, canon#191).

## D. Bindings (user-owned repos — PRs only, never release)

- leptris-ruby#165 (RelaxNG), #170 (Schematron), #171 (Diff),
  leptris-py#99 (Schematron) — all open, ready, user merges.
- moxml perf rows: parse 2.6-2.8x, serialize 2.4x, e2e 2.15x,
  xpath 31x, HTML 3.6-11x, C14N 23x AHEAD of Nokogiri; the 0.69x
  NS-read row CLOSED 2026-09-12 (four measurements — the deficit
  is moxml's own materializer layer; our C resolver and gem are
  measured clear).

## E. External (Jing / metanorma-pdfa#98 follow-ups)

- sshaw/ruby-jing#6 upstream dormant; the strategic fix (#878) is
  shipped — the standoc migration is the remaining step.
- metanorma/standoc#1244 + mnconvert-ruby#40 — user review/merge.

## F. Housekeeping

- ~20 stale local build* dirs (user's call; one-build-dir
  discipline going forward — Mac crash memory).
- Pre-existing warnings on main: html_parse.c:2160 (dual anonymous
  entity-table struct types) + sax/pull.c:447 (const discard).
  Both benign; fix in a cleanup PR.
- Known repo quirk: libxslt-suite fixtures committed with CRLF
  bytes while .gitattributes says text eol=lf (e.g. bug-80.xml)
  — any stat-cache invalidation shows them dirty with an
  EOL-only diff. Do NOT "fix" by renormalizing inside an
  unrelated PR.

## Graph to success

```
STAGE 0 template fence slice (feat/html-template) → v1.9.145
  │
STAGE 1 #659: template mode stack → walker reds → family tails
  → bindings html entries → #659 CLOSED
  │
STAGE 2 lanes 05/07/11/12/15: QT3 + dates + fn-items + versioned
  eval entries → language surface complete, suite-gated
  │
STAGE 3+4 user merges: ruby#165 → standoc off Jing → #878 CLOSED;
  #170 + py#99 → lane 16 CLOSED; #171 + canon#191 → lane 17 CLOSED
  │
STAGE 5 #682 scope call → lane 13 closed
  │
STAGE 6 website v2 (agent) fed by our measured scorecard
```

XSLT speed vs the field (current, gated): transform 5x, predicate
15.6x, value-of 135x, subtree-copy 2.27x, xsltproc wall >=2x,
HTML parse 2.9x, C14N 23x binding-armed — every USER-VISIBLE
feature is ahead; the only sub-parity row is in-process dispatch
(0.80x, ceiling-analyzed, awaiting the user's scope call #682).
