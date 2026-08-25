# 08 — Performance gate + competitor matrix (standing)

Benchmarks (benchmarks/xslt/): fragment-heavy, namespace-heavy,
number-heavy, key-lookup, document()-chain — one per fixed cluster.
Matrix: leptris vs xsltproc vs Saxon-HE (java -jar saxon-he.jar)
on the same inputs; CI job runs it and FAILS on >3% regression
vs our own baseline AND logs the gap vs competitors. Report table
in the PR (like the XPath one on the website).
