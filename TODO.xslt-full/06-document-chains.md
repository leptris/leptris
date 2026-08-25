# 06 — document() multi-file + remaining EMPTY (~16)

- document('file.xml', node) second-arg base resolution (same
  directory as the stylesheet — the suite's hrefs are relative).
- Cached docs share the rtf_chain lifetime model; cross-document
  nodes serialize with their OWN ns decls.
- id()/key() against document('') trees (bug-115, bug-160).
- Early-exit audit: any op_ handler returning -1 kills output —
  replace silent aborts with recoverable errors (MECE: recovery
  policy lives in ONE place — the walker).
