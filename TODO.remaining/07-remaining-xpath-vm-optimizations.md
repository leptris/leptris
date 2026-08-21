# 07 — XPath VM: remaining optimizations

From docs/124-xpath-domination-plan.md and the README Planned list:
- Extend the subtree-interval index to RELATIVE descendant queries
  (`.//x` from non-root contexts) — currently only absolute paths
  and root-context descendants hit the index.
- Pre-fold constant function args for the remaining string
  functions: concat(), contains(), substring().
- Linux MADV_HUGEPAGE for the arena (measured unmeasurable on
  macOS; may pay on Linux servers).
