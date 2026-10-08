# XSD test fixtures

## libxml2-cases/

Copied from libxml2's `test/schemas/` (MIT license, GNOME/libxml2) —
the fixture set our Nokogiri-parity target (libxml2's `xmlschema.c`)
runs itself. These are the seed corpus for the XSD tier-1
differential gate (#1075): each `.xsd` compiles through the engine,
and paired `.xml` instances validate against expected outcomes.

Upstream: https://gitlab.gnome.org/GNOME/libxml2 (-/tree/master/test/schemas)
Local reference clone: `~/src/external/libxml2` (shallow, fixtures only).
