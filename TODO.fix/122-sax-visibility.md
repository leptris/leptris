# TODO 122 — SAX shared-library visibility fix

## Problem
`src/include/taurus/sax/sax.h` declares all SAX entry points
(`taurus_sax_parse`, `taurus_sax_parser_create`, `taurus_sax_parser_feed`,
`taurus_sax_parser_free`, `taurus_sax_parser_set_streaming`) without
the `TAURUS_API` macro.

The library builds with `CMAKE_C_VISIBILITY_PRESET=hidden` (see
`src/include/taurus.h` docstring and TODO 80). Without `TAURUS_API`,
the SAX symbols are hidden from the link table when libtaurus is
built as a shared library — so `Taurus::SAX` (Ruby FFI) and any other
binding that wants streaming SAX cannot dlsym them.

The DOM / XPath headers don't have this issue because every public
function carries `TAURUS_API`. Only `sax/sax.h` was missed.

## Plan (single phase)
1. Add `TAURUS_API` to every public SAX function declaration in
   `src/include/taurus/sax/sax.h`.
2. Build a shared library locally and verify `nm -D` lists the SAX
   symbols.
3. Add a header-hygiene test that grep-checks the header for the
   annotation (mirrors existing HeaderHygiene tests for DOM/XPath).
4. ctest green on Linux + macOS.

## Why this is a fix, not a feature
SAX has been a documented public API since v0.1; the missing
annotation is a build-system defect that surfaces only under
`-fvisibility=hidden` + shared library build.

## Branch
`todo-122-sax-visibility`
