#!/usr/bin/env python3
"""#682 phase A: lxml/libxslt reference timing for the template-heavy
dispatch fixture. Generates the SAME xml/xsl as bench_dispatch_heavy.c
and reports best-of-9 for in-process lxml transforms. Run next to the
C bench on the same machine."""
import io
import time

from lxml import etree

NAMES, PER = 30, 80

xml = io.BytesIO()
xml.write(b"<doc>")
for n in range(NAMES):
    for i in range(PER):
        xml.write(b"<e%d k='%d'><v>%d</v></e%d>" % (n, i, i, n))
xml.write(b"</doc>")
doc = xml.getvalue()

xsl = io.BytesIO()
xsl.write(b"<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>")
xsl.write(b"<xsl:template match='/'><out>"
          b"<xsl:apply-templates select='*'/>"
          b"<xsl:apply-templates select='*' mode='m1'/>"
          b"<xsl:apply-templates select='*' mode='m2'/>"
          b"<xsl:call-template name='t0'/></out></xsl:template>")
for m in range(3):
    for n in range(NAMES):
        xsl.write(b"<xsl:template match='e%d' mode='m%d'>"
                  b"<b%d m='%d'><xsl:value-of select='@k'/></b%d>"
                  b"<xsl:call-template name='t%d'/></xsl:template>" % (n, m, n, m, n, n))
for n in range(NAMES):
    xsl.write(b"<xsl:template name='t%d'><c%d/><xsl:if test='position() &lt; 0'>"
              b"<xsl:call-template name='t%d'/></xsl:if></xsl:template>" % (n, n, n))
xsl.write(b"</xsl:stylesheet>")

sheet = etree.XSLT(etree.XML(xsl.getvalue()))
d = etree.fromstring(doc)
sheet(d)  # warmup
best = float("inf")
for _ in range(9):
    t0 = time.perf_counter()
    sheet(d)
    ms = (time.perf_counter() - t0) * 1000.0
    best = min(best, ms)
print("lxml dispatch-heavy %d templates / %d elements: %.2f ms (best of 9)"
      % (4 * NAMES, NAMES * PER, best))
