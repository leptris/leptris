#!/usr/bin/env ruby
# Generate deep.xml - 100-level deep document for testing deep recursion

DEPTH = 100

puts '<?xml version="1.0" encoding="UTF-8"?>'
puts '<root>'

# Generate deeply nested structure
DEPTH.times do |i|
  indent = '  ' * (i + 1)
  puts "#{indent}<level#{i} depth=\"#{i}\">"
end

# Leaf content
indent = '  ' * (DEPTH + 1)
puts "#{indent}<leaf>Deep content at level #{DEPTH}</leaf>"

# Close all levels
DEPTH.downto(0) do |i|
  indent = '  ' * (i + 1)
  puts "#{indent}</level#{i}>"
end

puts '</root>'