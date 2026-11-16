#!/usr/bin/env ruby
# frozen_string_literal: true

# Differential corpus runner (Ruby binding).
# Prints one canonical line per corpus doc:
#   docname|xpath_count|sha256(normalized_serialization)
# Normalization (see README.md): the XML declaration and DOCTYPE prolog are
# stripped; comparison starts at the root element.

require "digest"
require "leptris"

CORPUS = File.join(__dir__, "corpus")

# Fixed corpus order — keep byte-identical across run.rb / run.py / run.mjs.
DOCS = %w[
  basic.xml
  cdata.xml
  comments_pis.xml
  dtd_model.xml
  entities_dtd.xml
  mixed_deep.xml
  namespaces.xml
  xpath_target.xml
  01-namespaces.xml
  02-entities-predef.xml
  03-entities-numeric.xml
  04-cdata.xml
  05-comments-pi.xml
  06-doctype-internal.xml
  07-unicode.xml
  08-deep-nesting.xml
  09-mixed-content.xml
  10-big-attrs.xml
  11-whitespace.xml
  12-dup-xmlns.xml
  13-custom-dtd-entity.xml
  14-processing-instr-xml-decl.xml
  15-empty-and-selfclose.xml
  16-attrs-special-chars.xml
  17-rss-like.xml
  18-soap-like.xml
  19-xinclude.xml
  20-single-root.xml
  21-text-only-elems.xml
  22-long-text.xml
].freeze

XPATHS = {
  "basic.xml" => "count(//ns:elem)",
  "cdata.xml" => "count(//text)",
  "comments_pis.xml" => "count(//comment())",
  "dtd_model.xml" => "count(//item[@sku])",
  "entities_dtd.xml" => "count(//title)",
  "mixed_deep.xml" => "count(//p/*)",
  "namespaces.xml" => "count(//b:book)",
  "xpath_target.xml" => "count(//order[@status='active']/item)",
  "01-namespaces.xml" => "count(//*)",
  "02-entities-predef.xml" => "count(//*)",
  "03-entities-numeric.xml" => "count(//*)",
  "04-cdata.xml" => "count(//*)",
  "05-comments-pi.xml" => "count(//*)",
  "06-doctype-internal.xml" => "count(//*)",
  "07-unicode.xml" => "count(//*)",
  "08-deep-nesting.xml" => "count(//*)",
  "09-mixed-content.xml" => "count(//*)",
  "10-big-attrs.xml" => "count(//*)",
  "11-whitespace.xml" => "count(//*)",
  "12-dup-xmlns.xml" => "count(//*)",
  "13-custom-dtd-entity.xml" => "count(//*)",
  "14-processing-instr-xml-decl.xml" => "count(//*)",
  "15-empty-and-selfclose.xml" => "count(//*)",
  "16-attrs-special-chars.xml" => "count(//*)",
  "17-rss-like.xml" => "count(//*)",
  "18-soap-like.xml" => "count(//*)",
  "19-xinclude.xml" => "count(//*)",
  "20-single-root.xml" => "count(//*)",
  "21-text-only-elems.xml" => "count(//*)",
  "22-long-text.xml" => "count(//*)",
}.freeze

# Root element start: first `<` followed by a name character. Every prolog
# node (declaration, DOCTYPE subset, leading comments/PIs) starts `<?` or `<!`.
ROOT_START_RE = /<[A-Za-z_]/

DOCS.each do |name|
  doc = Leptris::XML.parse_file(File.join(CORPUS, name))
  count = doc.xpath(XPATHS.fetch(name))
  full = doc.to_xml
  body = full[full.index(ROOT_START_RE)..]
  puts "#{name}|#{count.to_i}|#{Digest::SHA256.hexdigest(body)}"
end
