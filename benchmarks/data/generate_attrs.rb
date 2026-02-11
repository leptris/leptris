#!/usr/bin/env ruby
# Generate attrs.xml - attribute-heavy document for testing attribute access

ELEMENT_COUNT = 100
ATTRS_PER_ELEMENT = 100

puts '<?xml version="1.0" encoding="UTF-8"?>'
puts '<root>'

# Generate elements with many attributes
ELEMENT_COUNT.times do |i|
  # Build attribute string
  attrs = (0...ATTRS_PER_ELEMENT).map do |a|
    "attr#{a}=\"value#{a}_#{i}\""
  end.join(' ')

  puts "  <element #{attrs}>"
  puts "    <content>Element #{i} with #{ATTRS_PER_ELEMENT} attributes</content>"
  puts "  </element>"
end

puts '</root>'