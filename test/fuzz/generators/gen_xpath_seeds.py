#!/usr/bin/env python3
"""Grammar-aware XPath/XQuery seed generator (SOTA C1 #2051). The
xpath_corpus's hand-written seeds are five lines; mutation explores
expression STRUCTURE from them slowly. This generator composes
expressions across the grammar: axis steps with predicates, function
calls (0/1/2-arg), unions, comparisons, arithmetic, numeric and
string literals, variables, path/absolute mixes, filtered primaries,
XQuery FLWOR spellings. Well-formed by construction; deterministic
under --seed. The nightly feeds them to the fuzz_xpath leg.

Usage: gen_xpath_seeds.py OUTFILE [--seed N] [--count N]
"""

import argparse
import os
import random

NAMES = ["a", "b", "item", "r", "node", "leaf", "span", "*"]
ATTRS = ["id", "kind", "status", "sku"]
AXES = ["child::", "descendant::", "following-sibling::",
        "preceding-sibling::", "ancestor::", "self::", "attribute::"]
FUNCS0 = ["position()", "last()", "count(//*)", "true()", "false()",
          "string-length(name())"]
FUNCS1 = ["string-length(.)", "number(@id)", "contains(@kind,'x')",
          "starts-with(name(),'a')", "normalize-space(.)",
          "string(.)", "not(.)", "round(number(@id))",
          "floor(number(@id))", "ceiling(number(@id))"]
FUNCS2 = ["substring(.,1,2)", "concat('x',.)", "substring-after(name(),'a')",
          "contains(.,'z')"]
LITS = ["1", "0", "2.5", "-3", "'x'", "\"y\"", "'a b'", "1e2", ".5"]


def predicate(rng, depth):
    r = rng.random()
    if r < 0.25:
        return str(rng.randint(1, 3))
    if r < 0.5:
        return rng.choice(FUNCS0 + FUNCS1)
    if r < 0.7:
        rhs = rng.choice(["1", "'x'", "true()"])
        op = rng.choice(["=", "!="])
        return f"@{rng.choice(ATTRS)}{op}{rhs}"
    return comparison(rng, depth - 1)


def step(rng, depth):
    axis = rng.choice(AXES) if rng.random() < 0.4 else ""
    name = rng.choice(NAMES)
    s = f"{axis}{name}"
    if depth > 0:
        while rng.random() < 0.5:
            s = f"{s}[{predicate(rng, depth - 1)}]"
    return s


def path(rng, depth):
    p = step(rng, depth)
    n = rng.randint(0, 2)
    for _ in range(n):
        sep = "//" if rng.random() < 0.2 else "/"
        p = f"{p}{sep}{step(rng, depth - 1)}"
    return p


def comparison(rng, depth):
    if depth <= 0:
        return rng.choice(LITS)
    lhs = operand(rng, depth - 1)
    op = rng.choice(["=", "!=", "<", ">", "<=", ">=", "!=", "="])
    rhs = operand(rng, depth - 1)
    return f"{lhs} {op} {rhs}"


def operand(rng, depth):
    r = rng.random()
    if r < 0.35:
        return path(rng, depth)
    if r < 0.55:
        return rng.choice(FUNCS0 + FUNCS1 + FUNCS2)
    if r < 0.7:
        return rng.choice(LITS)
    if r < 0.85:
        return f"({comparison(rng, depth - 1)})"
    return f"{operand(rng, depth - 1)} {rng.choice(['+', '-', 'div', 'mod'])} {rng.choice(LITS)}"


def expr(rng, depth):
    e = comparison(rng, depth)
    while rng.random() < 0.35:
        e = f"{e} | {path(rng, depth - 1)}"
    if rng.random() < 0.12:
        var = rng.choice(["$x", "$item", "$n"])
        inner = expr(rng, depth - 1).replace("'", "\\'")
        e = f"(for {var} in (1,2) return if ({e}) then {var} else ())"
    return e


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outfile")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--count", type=int, default=200)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    seen = set()
    lines = []
    for _ in range(args.count):
        e = expr(rng, rng.randint(1, 4))
        if e in seen or len(e) > 400:
            continue
        seen.add(e)
        lines.append(e)
    with open(args.outfile, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} expressions to {args.outfile}")


if __name__ == "__main__":
    main()
