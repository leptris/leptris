#!/usr/bin/env python3
"""Grammar-aware HTML seed generator (SOTA C1 #2051): documents that
anchor the tokenizer's STATE SPACE — foreign content (SVG/MATH with
integration points), CDATA-ish surprises, self-closing tags, weird
names, implied end tags, attributes with odd spellings, comments/
doctypes in odd positions. HTML parsing is error-recovery by design;
the generator exercises the recovery paths from INSIDE the grammar
rather than at byte-flip distance. Deterministic under --seed.

Usage: gen_html_seeds.py OUTDIR [--seed N] [--count N]
"""

import argparse
import os
import random

BLOCK = ["div", "p", "section", "li", "td", "tr", "table"]
FOREIGN = ["svg", "math"]
SVG_KIDS = ["path", "circle", "g", "foreignObject", "desc", "title",
            "text", "clipPath", "use"]
MATH_KIDS = ["mi", "mo", "mn", "mrow", "msup", "annotation-xml"]
WEIRD_TAGS = ["a<b", "foo.bar", "x:y", "font-face-uri", "set"]
TEXTS = ["hello", "42", "a < b", "<notatag", "&amp;amp", "  "]
ATTR_SPELLINGS = ['class="x"', "id=noquotes", "data-x='y'",
                  "DATA-UPPER=1", 'disabled', '=""',
                  'style="color:red"']


def attrs(rng):
    return (" " + " ".join(rng.sample(
        ATTR_SPELLINGS, rng.randint(0, 3)))) if rng.random() < 0.8 else ""


def html(rng, depth):
    out = []
    for _ in range(rng.randint(1, 4)):
        r = rng.random()
        if r < 0.30 and depth > 0:
            t = rng.choice(BLOCK)
            out.append(f"<{t}{attrs(rng)}>{html(rng, depth - 1)}</{t}>")
        elif r < 0.50:
            f = rng.choice(FOREIGN)
            kids = rng.choice(SVG_KIDS) if f == "svg" else rng.choice(MATH_KIDS)
            inner = "".join(
                f"<{kids}{attrs(rng)}{rng.choice(['', ' />', ' /'])}>"
                for _ in range(rng.randint(1, 3)))
            tail = rng.choice(["", "<![CDATA[x]]>", "</p>"])
            out.append(f"<{f}{attrs(rng)}>{inner}{tail}</{f}>")
        elif r < 0.58:
            out.append(rng.choice(TEXTS))
        elif r < 0.64:
            out.append(f"<!-- comment {rng.randint(0, 9)} -->")
        elif r < 0.70:
            out.append(f"<{rng.choice(WEIRD_TAGS)}{attrs(rng)}>")
        elif r < 0.76:
            out.append(rng.choice(["<br>", "<hr/>", "<img src=x>",
                                   "<input value=?>"]))
        elif r < 0.82:
            out.append(rng.choice(["<p>", "</p>", "<li>", "</li>"]))
        elif r < 0.88:
            out.append(f"<script{attrs(rng)}>if (a<b) x();</script>")
        elif r < 0.94:
            out.append(f"<style>p {{ color: red }}</style>")
        else:
            out.append("<template><td><a href=#>t</td></template>")
    return "".join(out)


def gen_doc(rng):
    parts = []
    if rng.random() < 0.7:
        parts.append(rng.choice([
            "<!DOCTYPE html>", "<!doctype html>",
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">",
        ]))
    parts.append("<html>")
    if rng.random() < 0.6:
        parts.append(f"<head><meta charset=utf-8><title>t</title></head>")
    parts.append(f"<body>{html(rng, rng.randint(1, 5))}</body>")
    parts.append("</html>")
    return "".join(parts) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--count", type=int, default=100)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    rng = random.Random(args.seed)
    for i in range(args.count):
        with open(os.path.join(args.outdir, f"gen{i:04d}.html"), "w",
                  encoding="utf-8") as f:
            f.write(gen_doc(rng))
    print(f"wrote {args.count} seeds to {args.outdir}")


if __name__ == "__main__":
    main()
