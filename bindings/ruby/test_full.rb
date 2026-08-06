require 'taurus'

xml = '<root><child id="1">hello</child><child id="2">world</child></root>'
doc = Taurus::Document.parse(xml)
root = doc.root
puts "root name: #{root.name}"
puts "root text: #{root.text}"
child = root.first_child_element
puts "first child: #{child.name}, id=#{child['id']}, text=#{child.text}"

puts "xpath count(//child): #{doc.xpath('count(//child)')}"

results = doc.xpath('//child')
puts "xpath nodeset: #{results.length} elements"
results.each_with_index do |e, i|
  puts "  [#{i}] id=#{e['id']}, text=#{e.text}"
end

puts "serialize (first 40 chars): #{doc.serialize[0..39]}..."
doc.free
puts 'ALL OK'
