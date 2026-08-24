# TODO.assemble — XInclude xml-mode, XPointer, xml:base, resolver

Phases:
- 01-xinclude-xml: parse="xml" splices the included document's
  children (adopt/merge machinery, fresh-parse fallback, xi:fallback
  honored, relative href resolution via xml:base chain, loop
  detection, accept/accept-language passthrough stubs)
- 02-xpointer: element() scheme (ID shorthand + child sequences),
  xpointer() scheme (XPath selection, first-node-wins, points/
  ranges explicitly out), xmlns() already shipped via ns bindings
- 03-xml-base: base URI resolution on parse (xml:base attributes
  merged per RFC 3986-ish relative resolution) + mutation updates
- 04-resolver-policy: pluggable resolver hook (file/local default),
  XML catalog support (parse + lookup), and the network policy —
  remote fetch stays OFF unless explicitly enabled per-parse
  (no_network default true; SSRF posture documented)
