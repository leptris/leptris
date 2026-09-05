#!/usr/bin/env ruby
# benchmarks/html/nokogiri_html_parse.rb — #659 lane: libxml2 (via
# Nokogiri::HTML, in-process) HTML parse throughput. Generates the
# byte-identical page as bench_html_parse.c. Best of N, MB/s.
require "nokogiri"

SECTIONS = 400

def gen_page
  out = +"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>" \
        "<title>Benckmark &amp; Page</title>" \
        "<link rel='stylesheet' href='/a.css'></head>" \
        "<body><header class='site'><nav><ul>"
  40.times { |i| out << "<li><a href='/item/#{i}' class='lnk'>Item&nbsp;#{i}</a></li>" }
  out << "</ul></nav></header><main>"
  SECTIONS.times do |s|
    out << "<section id='s#{s}'><h2>Section #{s} &mdash; &#8220;Notes&#8221;</h2>" \
           "<p class='text'>Lorem <b>ipsum</b> &amp; <i>dolor</i> sit amet " \
           "&lt;consectetur&gt; adipiscing elit &mdash; sed do eiusmod " \
           "tempor incididunt ut labore.</p>" \
           "<table class='data'><thead><tr><th>Id</th><th>Name</th>" \
           "<th>Value</th></tr></thead><tbody>"
    6.times do |r|
      out << "<tr><td>#{r * 7 + s}</td><td>row-#{s}-#{r}</td><td data-v='#{r}'>#{s}.#{format('%02d', r)}</td></tr>"
    end
    out << "</tbody></table><ul class='items'>"
    5.times do |u|
      out << "<li data-i='#{u}'><span class='k'>k#{u}</span>" \
             "<span class='v'>v&amp;#{u}</span></li>"
    end
    out << "</ul></section>"
  end
  out << "</main><footer><p>&copy; 2026 &#8212; bench</p></footer></body></html>"
  out
end

html = gen_page
reps = Integer(ENV.fetch("HTML_BENCH_REPS", "9"))
Nokogiri::HTML(html)  # warmup
best = Float::INFINITY
reps.times do
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  doc = Nokogiri::HTML(html)
  dt = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
  doc = nil
  best = dt if dt < best
end
printf("nokogiri(libxml2) html parse: %d bytes, %.0f us, %.1f MB/s (best of %d)\n",
       html.bytesize, best * 1_000_000, html.bytesize / best / 1_000_000, reps)
