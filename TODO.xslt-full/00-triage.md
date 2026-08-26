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
