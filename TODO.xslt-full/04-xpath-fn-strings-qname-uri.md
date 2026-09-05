# 04 — XPath fn: catalog: strings / QNames / URIs (#691-D)

Strings: codepoint-equal, normalize-space (exists), normalize-
unicode (NFC via utf8proc when built, else identity+doc), upper/
lower-case (exist), translate (exists), string-join (exists;
sequence arity), starts-with/ends-with (exist), substring-before/
after (exist; empty-match 3.1 semantics check vs Saxon),
concat (exists), iri-to-uri, encode-for-uri, decode-from-uri,
escape-html-uri, contains-token, format-integer (complete pattern
set: 9/A/a/I/i/〇 and pattern separators), adjust-string.
QNames: QName(prefix,local), resolve-QName, QName-from-string,
local-name-from-QName, prefix-from-QName, namespace-uri-from-QName,
node-name (element/attribute/PI), expanded-QName-to-string.

Gate: Xslt30.FnStrings spec (Saxon-probed) + suite green.


## Status 2026-09-04 — string/QName/URI tail shipped (v1.9.77+)

Shipped in the #691 scalar-tail wave: compare, codepoint-equal,
normalize-unicode (utf8proc forms; absent when built without
utf8proc), resolve-QName (prefix against in-scope namespaces via
the QName TLS channel), environment-variable,
available-environment-variables, unparsed-text / -lines /
-available (raw file reads, CWD-relative), uri-collection (empty
catalog). Remaining in this lane: analyze-string (needs the
fn:match/fn:non-match element model — XSLT xsl:analyze-string
exists; the fn form returns nodes), fn:format-number as a plain
XPath fn (the formatting core lives XSLT-side — layering decision
needed), fn:snapshot (needs detached-copy result trees).


## Status addendum 2026-09-04 (v1.9.79 candidate)

random-number-generator SHIPPED (PR #834): seeded xorshift64*
deterministic per seed, map carrier with 'number' in [0,1).
Remaining in this lane:
- fn:analyze-string — needs the fn:match/fn:non-match element
  model; build on a fresh anchored document (xq_anchor_owned_doc
  chain) + POSIX regex iteration; xsl:analyze-string exists
  XSLT-side as the semantic reference.
- fn:format-number as plain XPath — move the self-contained core
  (parse_pattern/PatternInfo/format_value, ~200 lines) from
  xslt_functions.c into a shared common/ module; XSLT keeps the
  decimal-format lookup and passes separator chars. Libxslt-suite
  format-number corners gate the move.
- fn:snapshot — detached deep copies via copy_subtree_detached,
  anchored on a fresh document through the owned-docs chain.
- RNG 'next'/'permute' members — need function-item closure state.


## Status addendum 2 (v1.9.80 candidate)

format-number SHIPPED (PR #836): the JDK pattern core is SSOT in
common/format_number.{h,c}; XSLT keeps the decimal-format lookup
and maps it into a spec; plain XPath gets the default-format fn.

BANKED INVARIANT (cost a SEGV to learn): the function registry is
LAST-REGISTRATION-WINS. Overriding layers (XSLT bridge, EXSLT)
register after the standard library; any handler that resolves its
closure through the per-function user_data slot MUST register via
xpath_function_registry_register_ud so the override carries its
own closure — a plain re-register leaves user_data NULL and the
handler dereferences garbage. Plain overrides reset user_data
explicitly. Symptom class: same-named fn silently behaving like
the base layer inside transforms (multi-byte df separators became
pattern suffix text).


## snapshot design note (2026-09-04, post-v1.9.80 attempt)

The obvious anchoring (fresh leptris_document_create + copies via
leptris_element_append_copy under a wrapper root + ctx owned_docs)
is INSUFFICIENT for plain leptris_xpath_eval: xpath_context_cleanup
frees owned_docs BEFORE the public eval returns (heap-use-after-free
on the borrowed copies at the first result_string call). The
owned-doc chain works only inside XSLT/XQuery lifecycles where the
context outlives result consumption. Correct fix: a document-
lifetime chain — an anchored-docs array on the SOURCE document
(leptris_document_free releases them), so snapshot copies live
exactly as long as the source tree. Field + free-path change +
specs; analyze-string needs the same chain (its constructed
fn:match/fn:non-match elements face the identical lifecycle).
RED spec was written first and reverted with the code.

## Status addendum 2026-09-05 (#846 fixed, PR #854)

analyze-string namespace resolution shipped: the root cause was NOT
in analyze-string or the resolver at all — constructed elements
never had their QNames split (full lexical "fn:match" in name,
prefix NULL), while every resolver compares name against the test's
LOCAL part. leptris_elem_split_qname at both creation paths gives
constructed elements the parser shape; one xmlns:fn declaration on
the result root resolves every descendant. Also in that PR:
op_element reads the element prefix instead of re-parsing the name,
and the HTML serializer gates void/raw/break rules to unprefixed
names (s:img is not the HTML img — bug-117). Lane 04 CLOSED.
