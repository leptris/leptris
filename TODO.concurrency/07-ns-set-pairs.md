# 07 — Array-based ns-set constructor

User report: ns_set_new + N×_add is N+1 FFI calls per query. A flat
array constructor makes it one — same wire shape as the c14n
inclusive-namespaces argument, so bindings share their adapter.

- New API: `leptris_xpath_ns_set_new_from_pairs(const char* const*
  flat, size_t pair_count)` — flat = [p1, u1, p2, u2, ...].
  NULL/empty-prefix entries rejected (LEPTRIS_ERROR_NULL_ARG).

DONE 2026-08-23: landed; spec covers 3-pair construction + rejection.
