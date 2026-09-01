# 10 — XSLT tunnel parameters

xsl:with-param tunnel="yes" + xsl:param tunnel="yes" — a separate
tunnel frame chain on XsltExec (ex->tunnel_vars) that apply-
templates/call-template propagate unchanged unless rebound; looked
up only by xsl:param tunnel declarations. MECE with the regular
frame chain (xslt_push_var family untouched).

Gate: Xslt30.TunnelParams spec (Saxon-probed) + suite green.
