# html5lib-tests (tree-construction snapshot)

Vendored snapshot of html5lib/html5lib-tests at commit
c67f90eacac14e022b1f2c2e5ac559879581e9ff — the last revision that
carried tree-construction/ (upstream moved those tests to
web-platform-tests afterwards). Same pin Nokogiri vendors.

Only tree-construction/*.dat + LICENSE are kept. Run by
test/test_html5lib.cpp; see TODO.xslt-full/14 for the harness
design and the mode-gate rationale (libxml2/Nokogiri tree shape is
the pinned behavior; the corpus red-list tracks the delta).
