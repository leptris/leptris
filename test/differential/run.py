#!/usr/bin/env python3
"""Differential corpus runner (Python binding).

Prints one canonical line per corpus doc:
  docname|xpath_count|sha256(normalized_serialization)
Normalization (see README.md): leptris.tostring emits no declaration and no
DOCTYPE, so the comparison is already root-element-onward; the same prolog
strip is applied for symmetry with run.rb / run.mjs.
"""

import hashlib
import os
import re

import leptris

CORPUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")

# Fixed corpus order — keep byte-identical across run.rb / run.py / run.mjs.
DOCS = [
    "basic.xml",
    "cdata.xml",
    "comments_pis.xml",
    "dtd_model.xml",
    "entities_dtd.xml",
    "mixed_deep.xml",
    "namespaces.xml",
    "xpath_target.xml",
]

XPATHS = {
    "basic.xml": "count(//ns:elem)",
    "cdata.xml": "count(//text)",
    "comments_pis.xml": "count(//comment())",
    "dtd_model.xml": "count(//item[@sku])",
    "entities_dtd.xml": "count(//title)",
    "mixed_deep.xml": "count(//p/*)",
    "namespaces.xml": "count(//b:book)",
    "xpath_target.xml": "count(//order[@status='active']/item)",
}

# Root element start: first `<` followed by a name character. Every prolog
# node (declaration, DOCTYPE subset, leading comments/PIs) starts `<?` or `<!`.
ROOT_START_RE = re.compile(r"<[A-Za-z_]")


def main() -> None:
    for name in DOCS:
        # parse(path) returns the Document; Document serialization keeps the
        # prolog/epilog comments and PIs (fromstring's Element subtree drops
        # them — see README normalization notes).
        doc = leptris.parse(os.path.join(CORPUS, name))
        count = doc.xpath(XPATHS[name])
        full = leptris.tostring(doc).decode("utf-8")
        body = full[ROOT_START_RE.search(full).start():]
        digest = hashlib.sha256(body.encode("utf-8")).hexdigest()
        print(f"{name}|{int(count)}|{digest}")


if __name__ == "__main__":
    main()
