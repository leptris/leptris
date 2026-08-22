# Remaining work — leptris

Snapshot taken at the v0.26.8 → leptris rebrand (2026-08-21).
Each file is one task; numbering is sequential, not priority.
Status: 01, 02, 03, 04, 06, 07, 08, 10 DONE/closed (see each file
for what actually shipped vs. what the TODO falsely recorded).
Remaining: 05 — Rust bindings, unblocked (bindgen over the settled
1.x public surface; the FFI drift gate and the DLL export check now
guard the surface it binds against).

Detailed history lives in docs/{n}-*.md and the perf ledger
(docs/114-close-pugixml-parse-gap.md and successors).
