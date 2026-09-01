# 07 — Serializer char corners (2 CHARREF + decl/indent polish)

- bug-74: newline+tab in attribute values → &#10;&#9; (numeric
  refs, not literal bytes) — the mutation-attr path bypasses the
  inline emitter I patched; route BOTH attr emitters through one
  escape table (SSOT — one table, two entry points).
- bug-159: HTML method percent-encoding + charset meta emission.
- XML decl: emit only when input had one OR method=xml with
  standalone; exact attribute order version/encoding/standalone.
