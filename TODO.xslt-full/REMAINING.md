# REMAINING — the complete open-work ledger (2026-09-05)

SSOT for everything still open after v1.9.86. Update entries as
work ships; delete entries as they close. Per-lane details live in
the numbered docs; this file is the index + the graph.

## A. In flight

- **#846** — analyze-string namespaces. PR #854 (constructed-QName
  split + F+O root declaration + op_element prefix + HTML sem flags
  gate). Awaiting CI → rebase-merge → release v1.9.87 → close.

## B. Open GitHub issues (leptris/leptris)

- **#682** — XSLT dispatch ~2.6x behind in-process libxslt. Protocol
  is fixture-first: build a 50+ template scorecard, re-profile (the
  2000-book profile is diffuse by design), THEN pick levers. Banked
  levers (TODO 13): per-eval nodeset/result allocator churn ~7%
  (scratch pool touches the document-scoped ownership model — needs
  a design conversation first), template name-keyed index for
  op_call_template (matters for template-heavy sheets only).
- **#659** — HTML parsing mode completion (lane 14 tail):
  1. html5lib tokenizer/tree corpus adoption (needs mode-gated
     adoption-agency + a JSON test harness; re-scoped away from
     Nokogiri-parity because implementing full AA would regress the
     libxml2-shape characterization specs).
  2. Nokogiri crawl-parity corpus on a real sample.
  3. Bindings: expose leptris_parse_html_string (ruby + python).

## C. TODO.xslt-full lanes — residual items

- **04 (strings/QName/URI)**: analyze-string namespace resolution
  ships with #854 — this lane is then CLOSED.
- **05 (dates)**: value-level gaps, non-blocking: adjust-*-to-
  timezone (needs a timezone model), format-date/time/dateTime,
  current-dateTime/date/time.
- **07 (function items) tail**: fn:sort with key/collation arity,
  for-each-pair uneven inputs past the zip. PUBLIC XPathResultType
  change for typed function items was the other item — verify
  whether #683's entry surface covered it; if not it stays open.
- **08 (maps/arrays)**: CLOSED (v1.9.57).
- **11 (XQuery core) tail**: qt3tests 1.0 subset adoption, Windows
  CLI harness for the XQuery driver.
- **12 (XQuery 3.1) tail**: qt3tests 3.1 subset adoption (language
  surface complete).
- **13 (perf)**: see #682 above — same workstream.
- **14 (HTML)**: see #659 above — same workstream.
- **15 (binding entries)**: leptris_xpath_eval_versioned (1.0-strict
  vs 3.x surface gating), xquery entries from 11, result-type
  extensions from 07 mirrored into both bindings. Gate: abi spec +
  mirrors drift check.

## D. Bindings (user-owned repos — PRs only, never release)

- leptris-ruby #127 and leptris-py #71 target v1.9.86 — retarget to
  v1.9.87 after the #846 release lands.
- After lane 15: new binding entries + parse_html_string exposure.

## E. External (Jing / metanorma-pdfa#98 follow-ups)

- sshaw/ruby-jing#6 (banner filter + java_opts clobber fix) —
  upstream dormant since 2022; if no response in ~1 week, fork under
  the metanorma org and cut a release from there (user decision).
- metanorma/metanorma-standoc#1244 (java_opts JVM props, drops
  _JAVA_OPTIONS mutation) — user review/merge.
- metanorma/mnconvert-ruby#40 (jdk.xml entity limits) — user
  review/merge.
- Strategic: JVM-free validation — revive lutaml/prax or native
  RELAX NG validation in libleptris (fits the parser+DTD+XPath/XSLT
  stack; removes Java-version limits + banner class of bugs).

## F. Housekeeping

- ~20 stale local build* dirs (user's call to consolidate; the Mac
  crash memory says one build dir discipline going forward).
- Pre-existing warning at html_parse.c:2160 (h_ent_index pointer
  typing from #848) visible under this build dir's flag set only.

## Graph to success

```
#846 (PR #854) ──merge──> release v1.9.87 ──> retarget ruby#127/py#71
     │
     ├─> #682 fixture-first profile ──> allocator/template-index levers ──> XSLT speed gate
     │
     └─> #659 lane 14: html5lib corpus + harness ──> crawl corpus ──> bindings expose parse_html_string
                                                                                      │
qt3tests 1.0/3.1 subsets (11/12) ──> lane 15 versioned entries ────────────────────┴─> bindings wave
Jing: standoc#1244 + mnconvert-ruby#40 + ruby-jing#6 merge ──> (fork if dormant) ──> pdfa#98 closed
Strategic: native RELAX NG validator (or PRAX revival) ──> JVM-free MN validation
```
