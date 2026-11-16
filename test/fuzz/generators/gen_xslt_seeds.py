#!/usr/bin/env python3
"""Grammar-aware seed generator for the XSLT/XPath fuzz families
(SOTA C1 #2051). Pure mutation from flat seeds reaches expression
STRUCTURE slowly; this generator composes valid-but-deep stylesheets
— paths, unions, predicates, arithmetic, function calls, sorts,
nested constructors — from a fixed vocabulary, giving libFuzzer
coverage anchors at expression depths mutation cannot build.

Each seed is a complete stylesheet fed through leptris_xslt_parse by
the nightly's fuzz_xslt leg. Deterministic under --seed.

Usage: gen_xslt_seeds.py OUTDIR [--seed N] [--count N]
"""

import argparse
import os
import random

XSL = "http://www.w3.org/1999/XSL/Transform"

NAMES = ["a", "b", "item", "r", "out", "node", "leaf", "span"]
ATTRS = ["id", "kind", "status", "sku", "mode"]
FUNCS0 = ["position()", "last()", "count(//*)"]
FUNCS1 = [
    "string-length(.)",
    "number(@id)",
    "contains(@kind,'x')",
    "starts-with(name(),'a')",
    "substring(.,1,2)",
    "normalize-space(.)",
    "concat('x',.)",
    "round(number(@id))",
]
AXES = ["child::", "descendant::", "following-sibling::", "ancestor::"]

TEXT_ATOMS = ["x", "42", "-7", "3.5", "true()", "0"]


def gen_path(rng, depth=0):
    """A relative or absolute location path with optional predicates."""
    parts = []
    if rng.random() < 0.2:
        parts.append("/")
    nsteps = rng.randint(1, 3)
    for i in range(nsteps):
        axis = rng.choice(AXES) if rng.random() < 0.25 else ""
        name = rng.choice(NAMES)
        if rng.random() < 0.15:
            name = "*"
        step = f"{axis}{name}"
        if rng.random() < 0.4:
            step += f"[@{rng.choice(ATTRS)}]"
        if rng.random() < 0.25:
            step += f"[{gen_scalar(rng, depth + 1)}]"
        parts.append(step)
    return "/".join(parts)


def gen_scalar(rng, depth=0):
    """A scalar expression: literal, function, arithmetic, comparison."""
    if depth > 2:
        return rng.choice(TEXT_ATOMS)
    r = rng.random()
    if r < 0.25:
        return rng.choice(TEXT_ATOMS)
    if r < 0.40:
        return rng.choice(FUNCS0 + FUNCS1)
    if r < 0.55:
        return f"{gen_scalar(rng, depth + 1)} {rng.choice(['+','-','*','div','mod'])} {gen_scalar(rng, depth + 1)}"
    if r < 0.70:
        return f"{gen_scalar(rng, depth + 1)} {rng.choice(['=','!=','<','>','<=','>='])} {gen_scalar(rng, depth + 1)}"
    if r < 0.80:
        return f"({gen_scalar(rng, depth + 1)})"
    return gen_path(rng, depth)


def gen_union(rng, depth=0):
    """1-3 alternatives joined by | — the union operator's surface."""
    n = rng.randint(1, 3)
    return "|".join(gen_path(rng, depth) for _ in range(n))


def gen_body(rng, depth=0):
    """A sequence constructor fragment."""
    out = []
    n = rng.randint(1, 3)
    for _ in range(n):
        r = rng.random()
        if depth < 2 and r < 0.35:
            sel = gen_union(rng, depth + 1)
            out.append(
                f"<xsl:for-each select=\"{xml_attr(sel)}\">"
                f"{gen_body(rng, depth + 1)}</xsl:for-each>"
            )
        elif depth < 2 and r < 0.5:
            test = gen_scalar(rng, depth + 1)
            out.append(
                f"<xsl:if test=\"{xml_attr(test)}\">"
                f"{gen_body(rng, depth + 1)}</xsl:if>"
            )
        elif depth < 2 and r < 0.6:
            sel = gen_path(rng, depth + 1)
            sort = gen_scalar(rng, depth + 1)
            out.append(
                f"<xsl:for-each select=\"{xml_attr(sel)}\">"
                f"<xsl:sort select=\"{xml_attr(sort)}\"/>"
                f"{gen_body(rng, depth + 1)}</xsl:for-each>"
            )
        elif r < 0.8:
            sel = rng.choice(FUNCS1 + [gen_path(rng, depth + 1)])
            out.append(f"<xsl:value-of select=\"{xml_attr(sel)}\"/>")
        else:
            name = rng.choice(NAMES)
            out.append(f"<{name}>@{rng.choice(ATTRS)}</{name}>")
    return "".join(out)


def xml_attr(s):
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


def gen_stylesheet(rng):
    """A full stylesheet: a root template whose body exercises the
    generated expression grammar."""
    body = gen_body(rng)
    return (
        f'<xsl:stylesheet xmlns:xsl="{XSL}" version="1.0">'
        f'<xsl:template match="/">'
        f"{body}"
        f"</xsl:template></xsl:stylesheet>"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--seed", type=int, default=20261117)
    ap.add_argument("--count", type=int, default=120)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    rng = random.Random(args.seed)
    for i in range(args.count):
        sheet = gen_stylesheet(rng)
        path = os.path.join(args.outdir, f"gen_{i:04d}.xsl")
        with open(path, "w", encoding="utf-8") as f:
            f.write(sheet)
    print(f"{args.count} seeds -> {args.outdir}")


if __name__ == "__main__":
    main()
