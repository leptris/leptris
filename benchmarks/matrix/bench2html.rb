#!/usr/bin/env ruby
# frozen_string_literal: true

# bench2html — convert bench_matrix YAML output to an HTML report.
#
# Reads one or more YAML files (one per library), generates a single
# self-contained HTML page with a feature-support matrix, per-feature
# comparison sections (clear winner indicators + ratio badges),
# resource usage, and Chart.js bar charts.
#
# Usage: bench2html.rb <yaml_files...> --output report.html

require 'yaml'
require 'erb'
require 'json'
require 'optparse'

LIB_COLORS = {
  'taurus'  => { bg: 'rgba(88,166,255,0.7)', border: 'rgba(88,166,255,1)', accent: '#58a6ff' },
  'pugixml' => { bg: 'rgba(63,185,80,0.7)', border: 'rgba(63,185,80,1)', accent: '#3fb950' },
  'libxml2' => { bg: 'rgba(248,81,73,0.7)', border: 'rgba(248,81,73,1)', accent: '#f85149' }
}.freeze

def read_libraries(paths)
  paths.filter_map do |path|
    data = YAML.safe_load(File.read(path))
    next unless data.is_a?(Hash) && data['benchmarks']

    { 'name' => data.dig('meta', 'library') || File.basename(path, '.yaml'),
      'version' => data.dig('meta', 'version') || '?',
      'platform' => data.dig('meta', 'platform') || '?',
      'benchmarks' => data['benchmarks'] }
  end
end

def ratio_badge(base, other)
  return '' if other.nil? || other.zero?
  r = base / other.to_f
  if r < 0.95
    "<span class='badge win'>#{format('%.1f×', 1 / r)} faster</span>"
  elsif r > 1.05
    "<span class='badge lose'>#{format('%.1f×', r)} slower</span>"
  else
    "<span class='badge tie'>≈ parity</span>"
  end
end

def build_html(libraries)
  lib_names = libraries.map { |l| l['name'] }
  all_ids = libraries.flat_map { |l| l['benchmarks'].map { |b| b['id'] } }.uniq

  # Group benchmarks by feature category
  features = {}
  all_ids.each do |bid|
    lib = libraries.find { |l| l['benchmarks'].any? { |b| b['id'] == bid } }
    b = lib['benchmarks'].find { |b2| b2['id'] == bid }
    feat = b['feature'] || 'other'
    features[feat] ||= []
    features[feat] << bid
  end

  # --- Feature support matrix ---
  support_rows = features.map do |feat, ids|
    cells = ["<td class='feat-name'>#{ERB::Util.html_escape(feat)}</td>",
             "<td class='feat-count'>#{ids.size}</td>"]
    libraries.each do |lib|
      supported = ids.count { |bid| lib['benchmarks'].any? { |b| b['id'] == bid } }
      if supported == ids.size
        cells << "<td class='full'>✓</td>"
      elsif supported.positive?
        cells << "<td class='partial'>#{supported}/#{ids.size}</td>"
      else
        cells << "<td class='none'>—</td>"
      end
    end
    "<tr>#{cells.join}</tr>"
  end.join("\n")

  support_header = lib_names.map { |n| "<th>#{ERB::Util.html_escape(n)}</th>" }.join
  support_matrix = <<~HTML
    <section id="support">
      <h2>Feature Support</h2>
      <p class="section-desc">Not every library implements every capability. Cells show how many benchmark shapes each library supports per feature.</p>
      <table class="support">
        <tr><th>Feature</th><th>Shapes</th>#{support_header}</tr>
        #{support_rows}
      </table>
    </section>
  HTML

  # --- Per-feature comparison sections ---
  feature_sections = features.map do |feat, ids|
    rows = ids.map do |bid|
      cells = ["<td class='bench-id'>#{ERB::Util.html_escape(bid)}</td>"]
      latencies = []
      tputs = []
      libraries.each do |lib|
        match = lib['benchmarks'].find { |b| b['id'] == bid }
        if match
          m = match['metrics'] || {}
          lat = m.dig('latency_us', 'min') || 0
          tput = m['throughput_mbs'] || 0
          latencies << lat
          tputs << tput
          cells << "<td>#{format('%.0f', lat)} µs</td>"
        else
          latencies << nil
          tputs << nil
          cells << '<td class="na">not supported</td>'
        end
      end

      # Determine winner and add badges
      valid = latencies.each_with_index.filter_map { |l, i| [i, l] if l }
      if valid.size > 1
        best = valid.min_by { |_, l| l }
        second = valid.sort_by { |_, l| l }[1]
        winner_lib = libraries[best[0]]['name']
        ratio = second[1].to_f / best[1]
        cells[best[0] + 1] = cells[best[0] + 1].sub('<td>', "<td class='winner'>") +
          "<span class='badge win'>★ #{format('%.1f×', ratio)} faster</span>"
        # Mark losers
        valid.each do |idx, lat|
          next if idx == best[0]
          r = lat.to_f / best[1]
          cells[idx + 1] = cells[idx + 1].sub('<td>', "<td class='loser'>")
        end
      end
      "<tr>#{cells.join}</tr>"
    end.join("\n")

    header = lib_names.map { |n| "<th>#{ERB::Util.html_escape(n)}</th>" }.join
    <<~HTML
      <section class="feature" id="feat-#{feat.downcase.gsub(/\s+/, '-')}">
        <h2>#{ERB::Util.html_escape(feat)}</h2>
        <table>
          <tr><th>Benchmark</th>#{header}</tr>
          #{rows}
        </table>
      </section>
    HTML
  end.join("\n")

  # --- Charts ---
  charts = all_ids.filter_map do |bid|
    labels = []
    latency = []
    libraries.each do |lib|
      labels << lib['name']
      match = lib['benchmarks'].find { |b| b['id'] == bid }
      latency << (match ? match.dig('metrics', 'latency_us', 'min') : nil)
    end
    next if latency.compact.empty?
    { 'id' => bid, 'labels' => labels, 'latency' => latency }
  end

  chart_divs = charts.each_with_index.map do |_c, i|
    "<div class='chart-box'><canvas id='chart#{i}'></canvas></div>"
  end.join

  bg_colors = libraries.map { |l| LIB_COLORS.dig(l['name'], :bg) || 'rgba(160,160,160,0.7)' }
  border_colors = libraries.map { |l| LIB_COLORS.dig(l['name'], :border) || 'rgba(160,160,160,1)' }
  chart_scripts = charts.each_with_index.map do |c, i|
    <<~JS
      new Chart(document.getElementById('chart#{i}'), {
        type: 'bar',
        data: { labels: #{c['labels'].to_json},
                datasets: [{ label: 'µs (min)', data: #{c['latency'].to_json},
                             backgroundColor: #{bg_colors.to_json},
                             borderColor: #{border_colors.to_json}, borderWidth: 1 }] },
        options: { responsive: true,
                   plugins: { title: { display: true, text: #{c['id'].to_json}, color: '#e6edf3', font: { size: 11 } } },
                   scales: { y: { beginAtZero: true, ticks: { color: '#8b949e' }, grid: { color: '#21262d' } },
                             x: { ticks: { color: '#8b949e' } } } }
      });
    JS
  end.join("\n")

  # --- Resource usage ---
  res_rows = all_ids.map do |bid|
    cells = ["<td>#{ERB::Util.html_escape(bid)}</td>"]
    libraries.each do |lib|
      match = lib['benchmarks'].find { |b| b['id'] == bid }
      if match
        m = match['metrics'] || {}
        mem = m['memory_peak_kb'] || 0
        cu = m['cpu_user_ms'] || 0
        cs = m['cpu_sys_ms'] || 0
        cells << "<td>#{mem} KB</td><td>#{format('%.1f/%.1f', cu, cs)} ms</td>"
      else
        cells << '<td class="na">—</td><td class="na">—</td>'
      end
    end
    "<tr>#{cells.join}</tr>"
  end.join("\n")

  res_header = lib_names.flat_map do |n|
    e = ERB::Util.html_escape(n)
    ["<th>#{e}<br>mem</th>", "<th>#{e}<br>cpu u/s</th>"]
  end.join

  lib_versions = libraries.map { |l| "#{ERB::Util.html_escape(l['name'])} #{ERB::Util.html_escape(l['version'])}" }.join(' vs ')
  platform = ERB::Util.html_escape(libraries.first&.dig('platform') || '?')
  ts = Time.now.strftime('%Y-%m-%d %H:%M')

  <<~HTML
    <!DOCTYPE html>
    <html lang="en">
    <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>XML Library Benchmark Matrix</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
    <style>
      :root {
        --bg: #0d1117; --panel: #161b22; --border: #30363d;
        --text: #e6edf3; --muted: #8b949e; --dim: #484f58;
        --blue: #58a6ff; --green: #3fb950; --red: #f85149; --amber: #d29922;
      }
      * { box-sizing: border-box; margin: 0; padding: 0; }
      body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', sans-serif;
             background: var(--bg); color: var(--text); padding: 24px; max-width: 1400px; margin: 0 auto; }
      header { text-align: center; margin-bottom: 32px; padding: 24px 0; border-bottom: 1px solid var(--border); }
      header h1 { font-size: 28px; color: var(--blue); margin-bottom: 6px; }
      header .subtitle { color: var(--muted); font-size: 16px; }
      header .meta { color: var(--dim); font-size: 13px; margin-top: 8px; }
      h2 { color: var(--blue); margin: 36px 0 12px; font-size: 20px; }
      .section-desc { color: var(--muted); font-size: 13px; margin-bottom: 12px; }
      section { margin-bottom: 24px; }
      table { border-collapse: separate; border-spacing: 0; width: 100%;
              background: var(--panel); border: 1px solid var(--border);
              border-radius: 10px; overflow: hidden; margin-bottom: 16px; }
      th { background: #1c2431; color: var(--blue); padding: 12px 16px;
           text-align: left; font-size: 13px; font-weight: 600;
           border-bottom: 1px solid var(--border); }
      td { padding: 10px 16px; border-bottom: 1px solid #21262d; font-size: 13px; }
      tr:last-child td { border-bottom: none; }
      tr:hover td { background: #1a1f28; }
      .bench-id { font-family: 'SF Mono', 'Cascadia Code', monospace; font-size: 12px; color: var(--muted); }
      .feat-name { font-weight: 600; }
      .feat-count { color: var(--muted); text-align: center; }
      .winner { color: var(--green); font-weight: 600; position: relative; }
      .loser { color: var(--red); }
      .na { color: var(--dim); font-style: italic; text-align: center; }
      .full { color: var(--green); text-align: center; font-size: 16px; }
      .partial { color: var(--amber); text-align: center; }
      .none { color: var(--dim); text-align: center; }
      .badge { display: inline-block; font-size: 11px; padding: 2px 8px; border-radius: 10px;
               margin-left: 6px; vertical-align: middle; white-space: nowrap; }
      .badge.win { background: rgba(63,185,80,0.15); color: var(--green); border: 1px solid rgba(63,185,80,0.3); }
      .badge.lose { background: rgba(248,81,73,0.15); color: var(--red); border: 1px solid rgba(248,81,73,0.3); }
      .badge.tie { background: rgba(210,153,34,0.15); color: var(--amber); border: 1px solid rgba(210,153,34,0.3); }
      .chart-container { background: var(--panel); border: 1px solid var(--border);
                         border-radius: 10px; padding: 20px; margin-bottom: 24px; }
      .chart-row { display: flex; gap: 12px; flex-wrap: wrap; }
      .chart-box { flex: 1 1 300px; min-width: 280px; max-width: 420px; }
      footer { margin-top: 48px; text-align: center; color: var(--dim); font-size: 12px;
               border-top: 1px solid var(--border); padding-top: 16px; }
      @media (max-width: 768px) { .chart-box { min-width: 100%; } }
    </style>
    </head>
    <body>
    <header>
      <h1>XML Library Benchmark Matrix</h1>
      <div class="subtitle">#{lib_versions}</div>
      <div class="meta">#{platform} &middot; #{ts}</div>
    </header>

    #{support_matrix}
    #{feature_sections}

    <section id="charts">
      <h2>Latency Charts</h2>
      <p class="section-desc">Bar height = latency µs (lower is better). Missing bars = feature not supported.</p>
      <div class="chart-container">
        <div class="chart-row">#{chart_divs}</div>
      </div>
    </section>

    <section id="resources">
      <h2>Resource Usage</h2>
      <p class="section-desc">Peak RSS delta (KB) and CPU time (user/sys ms) across all iterations.</p>
      <table>
        <tr><th rowspan="2">Benchmark</th>#{res_header}</tr>
        #{res_rows}
      </table>
    </section>

    <footer>
      Generated by <code>bench2html.rb</code> from <code>bench_matrix</code> YAML output.
      Lower latency is better. ★ marks the winner; ratios are vs the runner-up.
    </footer>

    <script>
    #{chart_scripts}
    </script>
    </body>
    </html>
  HTML
end

def main
  output = 'bench_report.html'
  parser = OptionParser.new
  parser.on('-o', '--output FILE') { |v| output = v }
  files = parser.parse(ARGV)
  abort 'usage: bench2html.rb <yaml_files...> [-o output.html]' if files.empty?

  libraries = read_libraries(files)
  abort 'error: no libraries found in YAML' if libraries.empty?

  File.write(output, build_html(libraries))
  puts "wrote #{output}"
end

main
