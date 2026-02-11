#!/usr/bin/env ruby
# Generate wide.xml - 1000-sibling document for testing sibling iteration

SIBLING_COUNT = 1000

puts '<?xml version="1.0" encoding="UTF-8"?>'
puts '<root>'

# Generate 1000 siblings at top level
SIBLING_COUNT.times do |i|
  puts "  <child id=\"#{i}\" index=\"#{i}\">"
  puts "    <name>Child #{i}</name>"
  puts "    <value>Value #{i}</value>"
  puts "    <data>#{('A'..'Z').to_a.sample(10).join}</data>"
  puts "  </child>"
end

puts '</root>'