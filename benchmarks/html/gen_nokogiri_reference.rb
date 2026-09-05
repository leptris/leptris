#!/usr/bin/env ruby
# benchmarks/html/gen_nokogiri_reference.rb — #659: the Nokogiri
# (libxml2) reference trees for the html5lib corpus. The goal of
# lane 14 is NOKOGIRI PARITY, not WHATWG conformance — libxml2
# itself fails most of the html5lib expectations, so this script
# records what Nokogiri::HTML actually produces for every corpus
# case, in the same "| "-tree .dat dialect the harness already
# parses. Output: test/html/nokogiri-tree-tests.dat (committed;
# CI never runs ruby).
#
# Case-splitting rules mirror test_html5lib.cpp exactly: a #data
# after this case's #document with NO blank line above is the
# html5lib fragment-pair continuation (skipped); #document-fragment
# and #script-on cases are skipped.

require "nokogiri"

SRC = File.expand_path("../../test/html/html5lib-tests/tree-construction",
                       __dir__)
OUT = File.expand_path("../../test/html/nokogiri-tree-tests.dat", __dir__)

def esc(s)
  s.gsub("\\", "\\\\\\\\").gsub('"', '\\"').gsub("\n", "\\n")
    .gsub("\t", "\\t").gsub("\r", "\\r")
end

def serialize(node, depth, io)
  pad = "  " * depth
  case node
  when Nokogiri::XML::Element
    attrs = node.attributes.values.map do |a|
      v = a.value.to_s
      " #{a.name}=\"#{esc(v)}\""
    end.join
    io << "| #{pad}<#{node.name}#{attrs}>\n"
    node.children.each { |c| serialize(c, depth + 1, io) }
  when Nokogiri::XML::Text
    io << "| #{pad}\"#{esc(node.content)}\"\n" unless node.content.empty?
  when Nokogiri::XML::Comment
    io << "| #{pad}<!--#{node.content}-->\n"
  end
end

written = 0
File.open(OUT, "w") do |out|
  Dir[File.join(SRC, "*.dat")].sort.each do |path|
    fname = File.basename(path)
    lines = File.read(path).split("\n", -1)
    cases = 0
    i = 0
    while i < lines.size
      break if i >= lines.size
      unless lines[i].start_with?("#data")
        i += 1
        next
      end
      data_lines = []
      skip = false
      orig = nil
      in_doc = false
      saw_doc = false
      i += 1
      while i < lines.size && !lines[i].start_with?("#")
        data_lines << lines[i]
        i += 1
      end
      # directives
      while i < lines.size
        l = lines[i]
        if l.start_with?("#data")
          # blank above = next case; else fragment continuation
          break if i > 0 && lines[i - 1].empty?
          skip = true
          break
        elsif l.start_with?("#document-fragment") || l.start_with?("#script-on")
          skip = true
          i += 1
          i += 1 while i < lines.size && !lines[i].start_with?("#")
        elsif l.start_with?("#errors")
          i += 1
          i += 1 while i < lines.size && !lines[i].start_with?("#")
        elsif l.start_with?("#document")
          saw_doc = true
          i += 1
          i += 1 while i < lines.size && lines[i].start_with?("|")
        else
          i += 1
        end
      end
      skip = true unless saw_doc
      cases += 1
      next if skip
      data = data_lines.join("\n")
      doc = Nokogiri::HTML(data)
      out << "#original #{fname}:#{cases}\n"
      out << "#data\n#{data}\n#errors\n#document\n"
      # libxml2 synthesizes an implicit doctype node for doctype-less
      # HTML; our parser does not — emit it only when the INPUT
      # carried one, so the parity comparator stays strict.
      had_doctype = data.match?(/\A\s*<!DOCTYPE/i)
      dt = doc.internal_subset
      if had_doctype && dt && dt.name && !dt.name.empty?
        out << "| <!DOCTYPE #{dt.name}>\n"
      end
      root = doc.root
      serialize(root, 0, out) if root
      out << "\n"
      written += 1
    end
  end
end
puts "wrote #{written} Nokogiri reference trees -> #{OUT}"
