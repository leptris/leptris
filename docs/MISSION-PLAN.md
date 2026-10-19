# The Mission Plan

How the mission — *replace the libxml2 era with a clean-room,
dependency-free XML stack that is simultaneously more conformant and
faster than the incumbents, delivered natively to every language it
reaches* — is fully achieved.

"Fully achieved" is not a feeling. The mission decomposes into four
falsifiable gates. Each gate has a measurement command whose output is
the verdict. Every number below is **last-known (date)** — Phase 0
re-baselines everything before any work is sequenced against it.

---

## Gate A — Conformance: no suite red anywhere

A feature isn't done until the suite says so. Conformance is the
moat: it is the one axis where "clean-room" must prove it gave up
nothing to libxml2.

| # | Suite | Exit | Last known |
|---|---|---|---|
| A1 | W3C XPath 1.0 | 438/438 pinned in CI | **done** — 438/438 |
| A2 | libxslt (XSLT 1.0) | 205/205 pinned (Linux-authoritative; macOS skips bug-169 iconv-encoding) | **done** — 205/205 (v1.9.19) |
| A3 | XsltFull | 27/27 + full suite green; XSLT gate (v1.10.0) stays met | **done** — gate MET (#1761) |
| A4 | QT3 / XQuery 3.1 | 0 unexplained pins; every pin has a recorded disposition | pins ~41–99 by slice (2026-09/10) — **re-pin fresh** |
| A5 | WHATWG/html5lib (#659) | 0 reds on the full corpus in HTML5 mode | 133/1753 reds (PR #1169, 2026-09-19) — **stale, re-measure** |
| A6 | XSD conformance lane | suite-complete per the lane's own queue | 769→795 (2026-10-14, queue #1691) |
| A7 | DTD phase 8 (TODO 91) | ENTITY/ENTITIES attribute resolution, parameter entities, choice-model backtracking | open |
| A8 | XInclude parse="xml" + xpointer (TODO 92) | spec-complete | open (parse="text" + fallback ship) |
| A9 | RelaxNG / C14N / SAX | parity maintained, no reds | green |

**Working rule (proven across A1–A3):** suite-driven fixes only —
read the failing case, name the root cause, red-first spec, fix,
re-run the whole suite. The recurring root causes are already
documented (RTF binding, foreign-doc patterns, §3.4 ordering,
xmlXPathFormatNumber, define-body sibling lists, carrier-by-pointer).
Every engine fix lands with its regression spec before the next case
is opened.

**Exit:** `scripts/validate.sh` green + each suite's command in
`VALIDATION.md` green + zero pins without a disposition file.

---

## Gate B — Performance: no published row trails

The scoreboard is published pairs — CPU-time medians, 31
interleaved reps, fresh build dirs, identical `CMAKE_BUILD_TYPE`
on both sides. The discipline *is* the result; phantom wins come
from stale build dirs, so the harness protocol is part of the gate.

| Row | Bar | Last known |
|---|---|---|
| parse | ≥ nokogiri/libxml2/pugixml | 2.6x — ahead |
| serialize | ≥ all | 2.6x — ahead |
| XPath | ≥ all | 18x — ahead |
| attr listing | ≥ all | 99x — ahead |
| build | ≥ all | 0.87x — ahead |
| walk+read (moxml) | ≥ 0.95x nokogiri | **done 2026-10-19** — ~195ns name / ~193ns content, 0.90–0.95x |
| append / attr-parse (pugixml lane 18) | ≥ parity | **RED 2.0x / 5.9x** — hash-prefilter lever named |
| DOM mutation rows | ≥ parity | RED — node-layout redesign, owner decision pending |
| zero-copy text (#1218 lever) | adopt if it wins any row | lever named, unmeasured |

**Working rules:** algorithms before struct bytes (the attr-parse and
append rounds proved it); measure deltas, not absolutes (spill and
extension-block effects are invisible in means — count events);
one local build dir at -j4, CI is the gate; macro-benchmarks claim
nothing without interleaved A/B medians.

**Exit:** every published row ≥ 1.00x median-of-runs on the published
harnesses + chokepoint regression gates in CI (the write-path and
attr-path chokepoints get the same treatment conformance got).

---

## Gate C — Delivery: the engine reaches every language, natively

The engine is nothing until a consumer can install it. Lockstep law:
runtime versions are `{engine-semver}.{binding-patch}`; every engine
release gets its runtime ride; ride branches push only after the tag
lands (TAG-RACE).

| # | Surface | Exit | Last known |
|---|---|---|---|
| C1 | leptris-ruby / -py / -ts rides | automated, proven (1.9.372 train: rubygems full matrix, PyPI, npm+provenance) | **done**, keep green |
| C2 | leptris-rs descriptor crate | `leptris-descriptor` published; safe-wrapper crate rides lockstep | feat/294 in flight, not published |
| C3 | moxml gem | cut (owner-dispatched) carries the walk+read adoption | merged on main, release pending owner |
| C4 | Prebuilt binaries everywhere consumers lack compilers | rubygems platform matrix ✓, PyPI wheels ✓, npm WASM ✓ | **done**, keep green |
| C5 | System packaging | vcpkg ✓; add homebrew / conan / deb formulas so "install libleptris" is one command per platform | open |
| C6 | Cargo publish lane | OIDC-free trusted publishing (crates-io-auth-action@v1) wired into the rust-release workflow; CARGO_REGISTRY_TOKEN secret set once | workflow shipped; secret/token state to verify in Phase 0 |

**Exit:** a consumer on any of the four runtimes installs a prebuilt
binary with one command; the descriptor ABI crate is consumable from
Rust with no engine checkout.

---

## Gate D — Adoption & durability: the libxml2 era actually ends

Replacing an era is an ecosystem event, not a benchmark. This gate is
the proof that downstream can and does move.

| # | Proof | Exit |
|---|---|---|
| D1 | Downstream corpus compiles end-to-end on leptris — metanorma/lutaml document sets parse, validate, transform, serialize with libxml2 removed from the path | green corpus runs recorded per release |
| D2 | Migration surface documented — nokogiri-compat face, per-adapter notes in moxml, a libxml2→libleptris migration guide in `docs/guide/` | guide ships |
| D3 | Security posture — nightly fuzzing ✓, ASAN CI ✓, ASAN Linux-only leak gate ✓; plus a documented advisory/reporting channel and a CVE-response note | security.md ships |
| D4 | 1.0 — semver freeze, ABI stability policy (export-surface gate stays green), documented platform matrix (ILP32/arm64/Intel legs, s390x gate) | v1.0.0 tagged through the release workflow, never by hand |
| D5 | Durability gates in CI — conformance suites and perf chokepoints fail the build on regression, so the mission cannot silently erode | gates wired into test.yml/asan.yml/fuzz-nightly.yml siblings |

**Exit:** D1 green + v1.0.0 shipped. That is the day the era ends.

---

## Phase sequencing

**Phase 0 — Re-baseline (first, everything depends on it).**
Run every Gate A command and every Gate B harness fresh. Produce one
numbered table dated today. Nothing in this plan is sequenced against
a stale number. (The #659 and QT3 figures above are already stale.)

**Phase 1 — Conformance closure (A4–A8).** Largest and longest. Order
by leverage: A5 (corpus tail — user-visible HTML correctness), A4
(QT3 pins — the XQuery story), A6 (XSD lane), A7/A8 (DTD/XInclude
completeness). One PR per fix, red-first spec with every fix,
suite-wide re-run per landing.

**Phase 2 — Performance closure (B reds).** Parallelizable with
Phase 1's tail: lane-18 hash prefilter first (named lever, biggest
gap), zero-copy text next, node-layout/mutation rows behind the
owner decision.

**Phase 3 — Delivery completion (C2/C3/C5/C6).** Descriptor crate
publish, moxml gem cut, packaging formulas. Small, mechanical, can
ride any release train.

**Phase 4 — Durability & adoption (D).** Migration guide, security
note, CI durability gates, then the 1.0 freeze. 1.0 ships **only
after** Gates A and B report zero reds — it is the seal, not a date.

## Invariants (the operating system)

These laws produced every number above; the plan executes inside
them, never around them:

- Falsifiability — red-first specs; a test that passes both sides is
  not a regression test.
- Lockstep + TAG-RACE — runtime versions ride engine semver; ride
  branches push only after the tag lands.
- Release workflow owns versions and tags; humans never tag by hand.
- Benchmark discipline — fresh build dirs, explicit build types,
  medians of interleaved reps, or the number does not exist.
- One PR per work item; branch → tests → ASAN → benchmark →
  rebase-merge; verify content on main after every merge.
- Binding repos release only through their own automated flows.

## The scoreboard of record

One table, updated at every release train: gate, command, last run,
verdict. When every row of Gate A and Gate B reads "green (dated)"
and Gate D1 is green, the mission is achieved — and v1.0.0 is the
tag that says so.
