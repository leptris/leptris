# 10 — API freeze checklist for 1.x

The leptris rebrand (this rename) breaks ABI and renames every
public symbol — the natural point to also settle the public
surface so 1.x can be frozen:

- types.h as the single canonical public-types header (TODO 99
  remainder: leptris.h historically re-declared some; verify the
  rename left exactly one definition of each public type).
- Public-API entry points write ONLY public status constants to
  their out-params (TODO 98 remainder; the internal enum must not
  leak — grep the status writes).
- Versioned symbol visibility for the shared library (ties into 06).
- Document the memory-ownership contracts ("Memory:" comments) as
  part of the 1.0 spec docs; they are the contract every binding
  builds on.
