#!/usr/bin/env python3
"""Grammar-aware XML seed generator (SOTA C1 #2051): documents whose
SHAPE anchors libFuzzer's parse-family coverage — nesting depth,
mixed content, CDATA, entity references (predefined and DTD-declared),
namespaces (default, prefixed, un-declaration), PIs, comments,
attribute storms. Pure byte-flip mutation reaches these structures
slowly; the generator composes them validly (well-formed: bound
prefixes, no duplicate attributes, DTD-backed entity refs) so the
parse fuzz leg starts from inside the grammar. Deterministic under
--seed.

Usage: gen_xml_seeds.py OUTDIR [--seed N] [--count N]
"""

import argparse
import os
import random

NAMES = ["r", "item", "entry", "node", "leaf", "span", "rec", "a", "b"]
ATTR_NAMES = ["id", "kind", "status", "sku", "lang", "type"]
NS = ["http://example.com/ns1", "http://example.com/ns2", "urn:leptris:test"]
PFX = ["n1", "n2", "t"]
TEXTS = ["hello", "42", "a &amp; b", "line1\nline2", "  spaced  ",
         "café", "✓"]
PREDEFINED = ["&amp;", "&lt;", "&gt;", "&quot;", "&apos;"]
DTD_ENTITIES = ["nbsp", "mdash", "ce"]


def gen_attrs(rng, used, pfx):
    out = []
    pool = [a for a in ATTR_NAMES if a not in used]
    rng.shuffle(pool)
    for name in pool[: rng.randint(0, 3)]:
        val = rng.choice(['"1"', "'x y'", '"a&lt;b"', f'"{rng.choice(TEXTS)}"'])
        used.add(name)
        out.append(f"{name}={val}")
    if pfx and rng.random() < 0.5:
        name = f"{pfx}.attr"
        if name not in used:
            used.add(name)
            out.append(f'{pfx}:attr="{rng.randint(0, 99)}"')
    return (" " + " ".join(out)) if out else ""


def element(rng, depth, st, root_extra=""):
    if depth <= 0:
        name = f"{st['pfx']}:leaf" if (st["pfx"] and rng.random() < 0.5) else "leaf"
        used = set()
        kind = rng.random()
        if kind < 0.2:
            return f"<{name}{gen_attrs(rng, used, st['pfx'])}/>"
        if kind < 0.4:
            return (f"<{name}{gen_attrs(rng, used, st['pfx'])}>"
                    f"<![CDATA[{rng.choice(['x < y', 'traps & gone', 'plain'])}]]>"
                    f"</{name}>")
        return f"<{name}{gen_attrs(rng, used, st['pfx'])}>{rng.choice(TEXTS)}</{name}>"

    prefixed = st["pfx"] and rng.random() < 0.4
    name = f"{st['pfx']}:{rng.choice(NAMES)}" if prefixed else rng.choice(NAMES)
    used = set()
    extra = None if root_extra else ""
    if extra is None:
        pass  # the root's xmlns choices live in root_extra
    elif rng.random() < 0.3:
        extra = f' xmlns="{rng.choice(NS)}"'
    elif rng.random() < 0.2:
        extra = ' xmlns=""'

    children = []
    for _ in range(rng.randint(1, 4)):
        kind = rng.random()
        if kind < 0.55:
            children.append(element(rng, depth - 1, st))
        elif kind < 0.65:
            children.append(f"<!-- comment {rng.randint(0, 99)} -->")
        elif kind < 0.72:
            children.append(f"<?pi{rng.randint(0, 9)} data?>")
        elif kind < 0.82:
            if st["has_dtd"] and rng.random() < 0.5:
                ent = f"&{rng.choice(DTD_ENTITIES)};"
            else:
                ent = rng.choice(PREDEFINED)
            children.append(f"t{ent}t")
        else:
            children.append(rng.choice(TEXTS))
    body = "".join(children)
    open_attrs = (gen_attrs(rng, used, st["pfx"]) + (extra or "")
                  + root_extra)
    if rng.random() < 0.15:
        return f"<{name}{open_attrs}/>"
    return f"<{name}{open_attrs}>{body}</{name}>"


def gen_doc(rng):
    has_dtd = rng.random() < 0.4
    dtd = ""
    if has_dtd:
        ents = "".join(
            f'<!ENTITY {e} "{rng.choice(["&#160;", "&#8212;", "custom"])}">'
            for e in DTD_ENTITIES)
        dtd = f"<!DOCTYPE r [<!ELEMENT r ANY>{ents}]>\n"
    pfx = rng.choice(PFX) if rng.random() < 0.5 else None
    st = {"pfx": pfx, "has_dtd": has_dtd}
    root_extra = f' xmlns:{pfx}="{rng.choice(NS)}"' if pfx else ""
    if rng.random() < 0.4:
        root_extra += f' xmlns="{rng.choice(NS)}"'
    decl = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            if rng.random() < 0.8 else "")
    return (decl + dtd
            + element(rng, rng.randint(1, 6), st,
                      root_extra=root_extra) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--count", type=int, default=100)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    rng = random.Random(args.seed)
    for i in range(args.count):
        doc = gen_doc(rng)
        with open(os.path.join(args.outdir, f"gen{i:04d}.xml"), "w",
                  encoding="utf-8") as f:
            f.write(doc)
    print(f"wrote {args.count} seeds to {args.outdir}")


if __name__ == "__main__":
    main()
