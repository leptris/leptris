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
