# 14 — HTML parsing mode (#659)

HTML4/5-tolerant parse into the standard DOM: implied end tags
(p/li/td/tr/...), attribute minimization, <script>/<style> raw
text, entities (named table), DOCTYPE legacy strings, optional
case-insensitive element names. API: LEPTRIS_PARSE_HTML flag +
leptris_parse_html_string; serializer method=html already ships.
Reference: libxml2 HTMLparser behaviors + the html5lib tree-
construction subset. Gate: html5lib tokenizer/tree tests subset +
Nokogiri parity corpus.
