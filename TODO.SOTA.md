# TODO.SOTA — the vision of done

The audit underneath this board: an XML stack is an **epistemic
instrument** — it decides what a document means. Its purpose is
trustworthy interpretation, made affordable everywhere. SOTA is
therefore not a feeling but four provable claims, each with a
measurement command whose output is the verdict:

1. **Conformance** — we interpret every document the way the
   specifications (and the reference suites) say. *Proof: zero reds
   on every adopted suite.*
2. **Performance** — correctness is free; we are faster than every
   incumbent on every published shape. *Proof: ≥1.00x on every row,
   interleaved medians.*
3. **Safety** — trust is auditable: memory-safe under sanitizers,
   fuzz-clean, reproducibly built. *Proof: clean lanes + per-release
   build manifests.*
4. **Universality** — the same semantics in every language; a
   document means the same thing through every runtime. *Proof: a
   cross-runtime differential corpus, byte-identical.*

Everything below is the task board for those four proofs. Every row
ends with a dated measurement; the scoreboard lives in
docs/MISSION-PLAN.md and is updated at every release.

---

## Gate A — Conformance: zero reds everywhere

### A1 WHATWG verdict parity: DONE — 1555/1555
**Measured 2026-10-21 (PR #1803, tagged v1.9.378): verdict
1555/1555, ZERO misses; tree 1555/1555; floor pinned at the full
corpus count.** The gate is presence-only — every corpus document
now reports a parse error exactly when html5lib does. The final
tail closed at the html5lib source sites: adoption-agency-1.3
(7 cases), menuitem endTagOther pair, tokenizer
expected-dashes-or-doctype / solidus-in-tag / attributes-in-end-tag,
AfterAfterFrameset trailing chars, BeforeHead
end-tag-after-implied-root, BeforeHtml unexpected-end-tag-before-html,
foreign end-tag mismatch, form-in-table both flavors, </form> table
voodoo, template foster-parenting-character, two-heads. Earlier
slices: initial mode, raw-text EOF, unterminated close, frameset,
fenced walk, InBody EOF allowed-set, in-select, foster-by-kind,
unknown-doctype (#1778-#1791), raw-text strays, non-html-root,
end-tag-too-early, after-body/frameset phases, start-tag phases,
select/table/foreign, need-space-after-doctype (#1794),
misplaced/stray-p, after-html codes, image (#1797, #1800). The
~270 remaining code-set divergences are prose-dialect labels from
older corpus datasets — reported, not gated.

### A2 QT3 / XQuery 3.1: zero unexplained pins
- [ ] re-pin fresh (the ~41–99 slice figures are dated)
- [ ] every remaining pin gets a disposition file or a fix
- [ ] ctor node-yield family: owned_docs transfer completion

### A3 XSD conformance lane: suite-complete
- [ ] the lane's own queue (769→795 → its ceiling) to zero

### A4 DTD phase 8 (TODO 91)
- [ ] ENTITY/ENTITIES attribute resolution
- [ ] parameter entities, choice-model backtracking

### A5 XInclude completeness (TODO 92)
- [ ] `parse="xml"` (ownership-transfer splice exists — wire it)
- [ ] xpointer

### A6 The universality proof (differential corpus)
- [ ] one curated corpus (the html5lib + QT3-winner + metanorma
      sample set) run through ruby/py/ts/rs bindings
- [ ] byte-identical outputs asserted in CI (the "one engine, same
      numbers everywhere" proof)

### A7 Maintain (already green; keep gated)
- [ ] XML 1.0, XPath 438/438, libxslt 205/205, XsltFull,
      C14N, RelaxNG, SAX records lane

## Gate B — Performance: every published row ≥1.00x

### B1 build row: ~2.9x → ≤1.0x (the #242 campaign)
**Measured 2026-10-21: honest baseline 2.72x (five fresh runs; the
old 1.86x record is NOT reproducible with identical code on today's
machine), ~1.9x after five merged moxml levers** (guard retirement,
namespace scope gate, nil-ivar priming, children-cache guard, and
the preprimed mint — the mint measured NEUTRAL and is not counted;
fast-attach was rejected as noise). Harness `wt/460-harness/build_bench.rb`,
31 interleaved reps, A/B pairs must alternate within ONE window
(machine drift is ±20% across session phases). StackProf: mint +
shape writes, attach pipeline, text= pipeline, GC. Decomposition on
record.
- [ ] per-element object budget: one Ruby object per created node
      (drop the shell entirely post-create-priming; the mint cache
      insert policy; the context registration)
- [ ] the root `to_binding` bridge (set_root path)
- [ ] `[]=` / `text=` write faces (nn_set_attribute pattern applied
      to text writes; one C frame per write)

### B2 walk row: 0.88x → much faster
Per-face reads already 3–5x ahead; the harness floor is shared
scaffolding. Memo law holds (dropping @children memo = 2x GC).
- [ ] hot-path audit of NodeSet#each / block dispatch
- [ ] publish the per-face table (the honest much-faster story)

### B3 whole-pipeline rows (new, published)
**Measured 2026-10-21: parse→query→serialize is GREEN — leptris
finishes in 0.533-0.573x of Nokogiri's CPU time (~1.8x faster),
three interleaved-median runs (`wt/460-harness/pipeline_bench.rb`,
250 records, 31 reps; same document and query both sides).**
- [x] parse→query→serialize pipeline row vs nokogiri (green, dated)
- [x] build→transform (XSLT) pipeline row — **GREEN 2026-10-21:
      build 250 records, XSLT 1.0 transform, serialize — leptris
      finishes in 0.492-0.515x of Nokogiri's CPU time (~2x faster),
      three interleaved-median runs
      (`wt/460-harness/xslt_pipeline_bench.rb`)**
- [ ] both to serialbench as the public scoreboard

### B4 durability
- [ ] CI perf regression gates on the chokepoints (parse, walk,
      build, serialize medians, fail on >5% regression)

### B5 remaining red rows
- [ ] pugixml lane-18 append/attr-parse (2.0x/5.9x; hash prefilter)
- [ ] zero-copy text lever (#1218) — adopt if it wins any row

## Gate C — Safety: trust is auditable

- [ ] C1 structure-aware fuzzing: grammar-aware corpora for
      XML/XPath/XSLT/HTML (grow beyond the three libFuzzer harnesses;
      corpus commit + crash-triage SLA)
- [ ] C2 sanitizer lanes over the risk families: records-drain,
      borrowed-slice + pool-recycle (the #506 class), iterparse
      keepalive — ASAN/UBSAN now, add TSAN (batch/worker surfaces)
- [ ] C3 reproducible builds everywhere: darwin toolchain pin done
      (ruby#512) — extend to linux/windows legs; publish a per-
      release build manifest (engine + binding hashes per platform)
- [x] C4 security.md: advisory channel, CVE-response process,
      dependency-free attestation — **shipped 2026-10-21 (PR #1804,
      v1.9.378)**: GitHub Security Advisories channel, 5-business-day
      acknowledgment target, coordinated disclosure, CVE assignment,
      scope table, hardening inventory tied to the lanes that actually
      run
- [ ] C5 the memory story documented: pool arena model, leak gates,
      the borrowed-slice lifetime contract

## Gate D — Universality: same semantics everywhere

- [ ] D1 leptris-descriptor published (crates.io; feat/294 branch
      lands), rides lockstep
- [ ] D2 system packaging: homebrew, conan, deb (apt repo)
- [ ] D3 cross-runtime differential CI (= A6, wired)
- [x] D4 libxml2→libleptris migration guide — **shipped 2026-10-21
      (PR #1810): docs/guide/migrating-from-libxml2.md** (ownership
      model, parse/tree/XPath/serialize/SAX/DTD mappings, deliberate
      differences); nokogiri-compat surface documented in the guide

## Gate E — 1.0 (the seal)

- [ ] E1 Gate A + B report zero reds (dated scoreboard)
- [ ] E2 semver freeze, ABI stability policy, platform matrix doc
- [ ] E3 v1.0.0 through the release workflow — never tagged by hand

---

## Invariants (the laws the board executes inside)

- Falsifiability: red-first specs; both-directions measurement.
- Lockstep + TAG-RACE; the release workflow owns versions and tags.
- One branch/PR per item; worktrees at wt/; never commit on main;
  never `git add -A`; verify the branch before every commit.
- Aliases before shims: unshadowed C faces upstream; consumers
  face-probe and plain-dispatch.
- Suites over opinions; the corpus is the reference.

## Definition of done

Every checkbox above carries a dated measurement command and its
recorded result; docs/MISSION-PLAN.md's scoreboard shows Gate A and
Gate B at zero reds; v1.0.0 is tagged. That day, the libxml2 era is
over wherever this engine ships.
