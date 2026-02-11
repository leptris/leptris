#!/usr/bin/env ruby
# Generate large.xml for benchmarking

def generate_book(id)
  <<~XML
    <book id="#{id}">
      <title>Book Title #{id}</title>
      <author>Author #{id}</author>
      <publisher>Publisher #{id % 10}</publisher>
      <year>#{2020 + (id % 5)}</year>
      <price>#{(id % 50) + 20}.99</price>
      <category>Category #{id % 20}</category>
      <isbn>978-0-#{sprintf('%06d', id)}-0</isbn>
      <reviews>
        #{(1..5).map { |r| "        <review rating=\"#{(r % 5) + 1}\">Review #{r} for book #{id}</review>" }.join("\n")}
      </reviews>
      <metadata>
        <pages>#{200 + (id % 500)}</pages>
        <language>#{['en', 'es', 'fr', 'de', 'ja'][id % 5]}</language>
        <format>#{['hardcover', 'paperback', 'ebook'][id % 3]}</format>
      </metadata>
    </book>
  XML
end

puts '<?xml version="1.0" encoding="UTF-8"?>'
puts '<catalog xmlns:pub="http://example.com/publisher">'

1000.times do |i|
  puts generate_book(i + 1)
end

puts '</catalog>'