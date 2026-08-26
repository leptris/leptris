# 00 — Triage board (updated 2026-08-26, suite 79/205)

Live census of test/xslt/open_cases.txt after the root-semantics,
namespace-axis, attr-set-chain, and serializer batches:
- ~92 DIFF (mixed: number formatting, sort, document(), strip-space
  corners, AVT quirks, stale-fixture bug-124)
- ~17 NUMBER, ~9 SORT, ~8 KEY, ~7 DOCUMENT, ~6 EMPTY/crash
- Windows-only divergences gated on the list: bug-93, bug-102
  (xsl:import resolution on Win32), bug-118 (copy-of select='/'
  document copy). Fix the Win32 path/join in the import resolver and
  the document-copy path, then un-gate.

# 00 — Triage harness (standing tooling)

The open list is living state, not a snapshot. Ship the triage as a
script (scripts/xslt_triage.sh) so every PR regenerates it:
- Runs each open case via the fork-isolated runner.
- Buckets by signature: EMPTY / CRASH / NS-OUT / CHARREF / PI-OUT /
  DIFF with one-line diffs.
- Rewrites test/xslt/open_cases.txt; CI fails if a case in the list
  passes or a case outside it fails (already enforced by the suite
  runner — this script keeps the list honest).

Done: the ad-hoc python used so far; productize it (SSOT: one
script, used by CI + devs).

---
## Session log 2026-08-26 (72/205)

Landed: cdata-section-elements + merged CDATA runs; prefix/ns-preserving
copies; top-level comment/PI materialization (before/after root chains);
xsl:number any-level node-kind walk; xsl:comment/PI content capture;
HTML PI serialization; RTF variables bind the fragment document node
(fragment chains = doc children on all three axis paths); namespace-gated
extension-function local-name fallback; attribute nodes in template
matching (@-patterns, copy, unified node-kind matcher); control-char
attr escaping on the mutation path.

Remaining census: ~85 DIFF, ~15 NUMBER, 9 SORT, 8 KEY, 7 DOCUMENT,
~5 crash. Windows gates: bug-93, bug-102 (import path), bug-118.

---
## Session log 2 (79/205, HEAD 72d2974)

Landed after the morning batch: attribute nodes in template matching
(@-patterns + unified node-kind matcher used by BOTH the match and the
priority rescan); xsl:copy of attributes; control-char attr escaping on
the mutation serializer path; §2.4 stylesheet whitespace (strip ALL
ws-only text — LRE indentation does NOT survive; xsl:text and
xml:space=preserve excepted — worth +4); in-scope (ancestor-walked)
namespace copies with redundant-declaration suppression (bug-128);
self::node() from attribute nodes; ancestor-or-self reverse document
order; document() → document node (bug-153) + foreign doc nodes resolve
their OWN root (bug-29- inclusion-loop crash); RTF variables bind the
fragment document node (bug-112/143/144); namespace-gated extension
local-name fallback.

Next clusters: ~80 DIFF (mixed one-offs — AVT-in-attribute-name
{name()} cases bug-195/196/197, format-number grouping bug-75/95,
xsl:number NaN→0 quirk bug-187), 16 NUMBER, 8 SORT, 7 KEY, ~5 crash.
Windows gates: bug-93, bug-102 (xsl:import path resolution on Win32),
bug-118 (copy-of select='/').

---
## Session log 3 (102 → 129/205, HEAD after 934acb9 + 5 commits)

Reference-oracle upgrade: libxml2 2.16.0 + libxslt checkouts at
~/src/external — all serializer behavior now ported from source
(HTMLtree.c htmlNodeDumpInternal, xsltutils.c xsltSaveResultTo),
not empirical probing.

Landed:
1. libxml2-parity HTML output (e709dad): serializer html_method
   mode — element descriptor table (EMPTY/INLINE/BLOCK, bsearch),
   the four newline sites, zero nesting spaces, p/pre/param
   parent-side rule; DOM-level <meta charset> injection
   (htmlSetMetaEncoding port); §16.1 unnamespaced-html-root default
   method + indent tri-state (-1: html yes, xml no); non-void empty
   → open+close pairs; full void-element set.
2. position() (2cb8cc5): exec carries current_pos; for-each +
   apply-templates (selection + default child walk) set it; new
   leptris_xpath_compiled_eval_ctx seeds ctx->context_position
   (VM + interpreter read the same field).
3. strip-space '*' (934acb9): NameTest wildcard in name_in_list;
   zero-length text children are stripped nodes (parse never
   creates them) — skipped in the child walk.
4. call-template §11 scope (later commit): exec snapshots the
   globals-only chain head; call resets there.
5. copy-of verbatim mixed content: every child kind in document
   order (was string-value + elements only).
6. Element-less results keep the XML declaration (§16.1).
7. EXSLT func module (3 commits): func:function/func:result/
   func:param — registry bindings, globals-only scope, RTF
   node-set returns, caller-context arg binding, function-ns
   output exclusion (pre-scan before template compile).
8. Namespace-URI pattern matching: XsltTemplate.ns (build_ns_
   context), pattern ladder evaluates through it; selection-loop
   temp template keeps .ns; /name fast path resolves the test
   prefix through ctx->ns_set. Cross-prefix matches now correct.

Suite: 129/205. Remaining census (76): 35 no-marker one-offs,
7 xsl:number, 7 namespace, 6 document(), 5 import, 4 id(), 3 key,
2 format-number, 2 exsl, ~10 more. Named next targets:
- bug-53: DTD ATTLIST DEFAULT attribute materialization at parse
  (parser-level feature — the dtd/ model stores default_value but
  nothing applies it on the parse path)
- bug-83: HTML URL-escaping of href attribute values (%20)
- bug-33-: script/style raw text (serializer isRaw/dataMode)
- bug-216: post-root document text (source trailing \n)
- Windows gates: bug-93, bug-102, bug-118 — now PASSING on POSIX,
  removed from the worklist (runner is POSIX-forked)

---
## Session log 4 (129 → 131/205 + architecture/perf pass)

Architecture review (report: architecture-review-20260826-xslt-perf.html,
4 candidates) — ALL IMPLEMENTED:

1. exec-scoped eval environment: document-cached function registry
   (invalidate on custom-fn register / exslt_enable / transform
   enter-exit) + dirty-skip varset mirror. Transform perf:
   bug-5- 115.3 → 48.5 ms (2.4x), doc 0.204 → 0.056 ms (3.6x).
2. HTML single-owner in the serializer: voids, non-void pairs,
   attr escaping (raw ', href/src URI %XX), rawtext elements
   (script/style/...), PI '>' close, indent=no semantics, method
   tri-state. to_html_method + html_post_pass DELETED (~200 lines).
   Suite: bug-83 minimal shape exact.
3. position() SSOT folded into leptris_xpath_compiled_eval_ctx.
4. Recursive serializer walker DELETED (-369/+64): heap-growable
   frames (2000-deep verified, ASAN-clean).

Latent bugs fixed along the way (ASAN-exposed):
- xslt_capture_children_text: stale rtf_text_cap overflowed the
  fresh buffer on nested captures
- buffer_create: uninitialized cdata_names/cdata_count read as the
  cdata list in every options-NULL serialization
- text-node base.raw never initialized in either creator
- leptris_node_prepend_child SEVERED the old first child's sibling
  link — every prepend dropped the children-chain tail (public API
  bug, exposed by the meta injection)

Known-environmental: test_cli spawns die with SIGKILL on this
machine (binary runs clean from shell; reproduced on HEAD stashed).

Suite 131/205; 983/983 ctest green. Remaining 74: same census as
log 3 minus closed items (bug-33-/83/159 cluster partially closed).
