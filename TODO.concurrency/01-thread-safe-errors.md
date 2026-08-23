# 01 — Thread-safe error reporting + per-document error slots

User report (leptris-ruby): `leptris_last_error` returns a
process-global string — races under concurrent parses from threaded
apps.

- error.c: `error_message` / `last_error` become LEPTRIS_THREAD_LOCAL
  (fixes the race under the one-document-per-thread contract).
- `struct leptris_document` gains a `last_error_message[256]` slot;
  every public parse entry point snapshots the thread-local message
  into it on failure.
- New public API: `leptris_document_last_error(doc)` — the message
  from this document's most recent failing parse (NULL when none or
  on NULL doc). Doc-owned; lives until leptris_document_free.
- `leptris_last_error` stays (legacy), now documented as the
  best-effort THREAD-LOCAL last message.

DONE 2026-08-23: all four landed; specs cover per-doc slots under
concurrent parses and the documented legacy semantics.
