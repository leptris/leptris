# 04 — One canonical status-message function

User report: `leptris_status_string` and `leptris_error_message`
have overlapping contracts; bindings guess which is richer. Since the
phantom implementation landed, error_message delegates to
status_string — identical output.

- Canonical: `leptris_status_string`.
- `leptris_error_message` documented as a deprecated-but-stable
  alias in BOTH doc blocks (kept: removing it breaks the Ruby/Python
  mirrors already shipping).

DONE 2026-08-23: doc blocks updated on both.
