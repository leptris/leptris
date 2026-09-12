#!/usr/bin/env python3
"""#682 XSLT scorecard: lxml/libxslt reference timings for the lane
fixtures that lack a twin (heavy already has heavy_lxml.py).

Mirrors the C benches exactly — same fixtures, same protocols: best of 9
for dispatch/pred/valueof/subtree (transform keeps its bench_run
mean-of-400). Compile once, apply-only timing. Run next to the C benches
on the same machine:

    python3 benchmarks/xslt/scorecard_lxml.py
"""
import copy
import io
import time

from lxml import etree


def best_of_9(fn):
    fn()
    best = float("inf")
    for _ in range(9):
        t0 = time.perf_counter()
        fn()
        best = min(best, (time.perf_counter() - t0) * 1000.0)
    return best


def gen_dispatch(books=2000):
    # mirrors bench_dispatch.c
    xml = io.BytesIO()
    xml.write(b"<catalog>")
    for i in range(books):
        xml.write(
            b"<book id='%d'><title>t</title><author>a</author></book>" % (i,)
        )
    xml.write(b"</catalog>")
    xsl = (
        b"<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
        b"<xsl:template match='/'><out>"
        b"<xsl:apply-templates select='//book'/></out></xsl:template>"
        b"<xsl:template match='book[title]'><b id='{@id}'>"
        b"<xsl:apply-templates select='*'/></b></xsl:template>"
        b"<xsl:template match='title'><t><xsl:value-of select='.'/></t></xsl:template>"
        b"<xsl:template match='author'><a><xsl:value-of select='.'/></a></xsl:template>"
        b"</xsl:stylesheet>"
    )
    return xml.getvalue(), xsl


def gen_pred(n=120, items=2400):
    # mirrors bench_dispatch_pred.c
    xml = io.BytesIO()
    xml.write(b"<root>")
    for i in range(items):
        xml.write(b"<item k='%d' v='v%d'/>" % (i % n, i))
    xml.write(b"</root>")
    xsl = io.BytesIO()
    xsl.write(
        b"<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
    )
    for i in range(n):
        xsl.write(
            b"<xsl:template match=\"item[@k='%d']\"><o%d>"
            b"<xsl:value-of select='@v'/></o%d></xsl:template>" % (i, i, i)
        )
    xsl.write(b"</xsl:stylesheet>")
    return xml.getvalue(), xsl.getvalue()


def gen_valueof(n=50000):
    # mirrors bench_valueof.c
    xml = io.BytesIO()
    xml.write(b"<r>")
    for _ in range(n):
        xml.write(b"<t>abcdefgh</t>")
    xml.write(b"</r>")
    xsl = (
        b"<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>"
        b"<xsl:template match='/'>"
        b"<xsl:for-each select='//t'>"
        b"<xsl:value-of select='.'/>"
        b"</xsl:for-each></xsl:template></xsl:stylesheet>"
    )
    return xml.getvalue(), xsl


def gen_transform(books=499):
    # mirrors benchmarks/xpath/bench_xslt_transform.c
    xml = io.BytesIO()
    xml.write(b"<catalog>")
    for i in range(books):
        xml.write(
            b"<book id='%d' price='%d'><title>Book %d</title></book>"
            % (i, i % 200, i)
        )
    xml.write(b"</catalog>")
    xsl = (
        b"<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        b" version='1.0'><xsl:template match='/'>"
        b"<out><xsl:for-each select=\"//book[@price > 100]\">"
        b"<b><xsl:value-of select='@id'/></b></xsl:for-each></out>"
        b"</xsl:template></xsl:stylesheet>"
    )
    return xml.getvalue(), xsl


def gen_subtree(books=100):
    # mirrors bench_subtree_copy.c
    xml = io.BytesIO()
    xml.write(b"<catalog version='2.0'>")
    for i in range(books):
        xml.write(
            b"<book id='%d' lang='en'><title>Book %d</title>"
            b"<author id='a%d'>Author %d</author>"
            b"<price currency='USD'>%d.99</price></book>" % (i, i, i, i, i)
        )
    xml.write(b"</catalog>")
    return xml.getvalue()


def run_apply(name, xml_bytes, xsl_bytes):
    doc = etree.parse(io.BytesIO(xml_bytes))
    transform = etree.XSLT(etree.parse(io.BytesIO(xsl_bytes)))
    transform(doc)
    best = best_of_9(lambda: transform(doc))
    print(f"lxml {name}: {best:.2f} ms (best of 9)")


def main():
    print(
        f"lxml {etree.__version__} / libxml2 {etree.LIBXML_VERSION} / "
        f"libxslt {etree.LIBXSLT_VERSION}"
    )
    run_apply("dispatch 2000 books", *gen_dispatch())
    run_apply(
        "pred-pattern dispatch 120 templates / 2400 items", *gen_pred()
    )
    run_apply("value-of . x50000", *gen_valueof())

    doc = etree.parse(io.BytesIO(gen_transform()[0]))
    transform = etree.XSLT(etree.parse(io.BytesIO(gen_transform()[1])))
    transform(doc)
    times = []
    for _ in range(400):
        t0 = time.perf_counter()
        transform(doc)
        times.append(time.perf_counter() - t0)
    print(f"lxml xslt apply //book[@price>100] mean "
          f"{sum(times) / len(times) * 1e6:.2f} us")

    root = etree.parse(io.BytesIO(gen_subtree())).getroot()

    def copy_200():
        for _ in range(200):
            copy.deepcopy(root)

    copy_200()
    best = float("inf")
    for _ in range(9):
        t0 = time.perf_counter()
        copy_200()
        best = min(best, (time.perf_counter() - t0) * 1000.0 / 200.0)
    print(f"lxml element_copy 100-book subtree: {best:.3f} ms/copy "
          f"(best of 9x200)")


if __name__ == "__main__":
    main()
