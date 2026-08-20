#!/usr/bin/env ruby
# frozen_string_literal: true

# bench2html — convert bench_matrix YAML output to an HTML report.
#
# Reads one or more YAML files (one per library), generates a single
# self-contained HTML page with comparison tables and Chart.js bar charts.
#
# Usage: bench2html.rb <yaml_files...> --output report.html
#
# Uses only Ruby stdlib (yaml, erb, json).

require 'yaml'
require 'erb'
require 'json'
require 'optparse'

def read_libraries(paths)
  paths.flat_map do |path|
    data = YAML.safe_load(File.read(path), permitted_classes: [], aliases: false)
    next [] unless data.is_a?(Hash) && data['benchmarks']

    data['benchmarks'].map do |b|
      m = b['metrics'] || {}
      {
        'id' => b['id'],
        'feature' => b['feature'],
        'input' => b['input'],
        'latency_min' => m['latency_us']&.fetch('min', nil),
        'latency_median' => m['latency_us']&.fetch('median', nil),
        'throughput_mbs' => m['throughput_mbs'],
        'cpu_user_ms' => m['cpu_user_ms'],
        'cpu_sys_ms' => m['cpu_sys_ms'],
        'memory_peak_kb' => m['memory_peak_kb']
      }
    end
    [{ 'name' => data.dig('meta', 'library') || File.basename(path, '.yaml'),
       'version' => data.dig('meta', 'version') || '?',
       'platform' => data.dig('meta', 'platform') || '?',
       'benchmarks' => data['benchmarks'] }]
  end.compact
end

def build_html(libraries)
  lib_names = libraries.map { |l| l['name'] }
  all_ids = libraries.flat_map { |l| l['benchmarks'].map { |b| b['id'] } }.uniq

  # Summary table rows
  summary_rows = all_ids.map do |bid|
    cells = ["<td>#{ERB::Util.html_escape(bid)}</td>"]
    latencies = []
    libraries.each do |lib|
      match = lib['benchmarks'].find { |b| b['id'] == bid }
      if match
        m = match['metrics'] || {}
        lat = m.dig('latency_us', 'min') || 0
        tput = m['throughput_mbs'] || 0
        latencies << lat
        cell = format('%.0f µs', lat)
        cell += format(' / %.1f MB/s', tput) if tput.to_f.positive?
        cells << "<td>#{cell}</td>"
      else
        latencies << nil
        cells << '<td class="na">—</td>'
      end
    end
    # Highlight winner
    valid = latencies.each_with_index.filter_map { |l, i| [i, l] if l }
    if valid.size > 1
      best = valid.min_by { |_, l| l }
      cells[best[0] + 1] = cells[best[0] + 1].sub('<td>', '<td class="winner">')
    end
    "<tr>#{cells.join}</tr>"
  end.join("\n")

  header = lib_names.map { |n| "<th>#{ERB::Util.html_escape(n)}</th>" }.join
  summary_table = <<~HTML
    <h2>Full Matrix (latency µs min / throughput MB/s)</h2>
    <table>
      <tr><th>Benchmark</th>#{header}</tr>
      #{summary_rows}
    </table>
  HTML

  # Resource usage table
  res_header = lib_names.flat_map do |n|
    n_esc = ERB::Util.html_escape(n)
    ["<th>#{n_esc} mem</th>", "<th>#{n_esc} cpu u/s</th>"]
  end.join
  res_rows = all_ids.map do |bid|
    cells = ["<td>#{ERB::Util.html_escape(bid)}</td>"]
    libraries.each do |lib|
      match = lib['benchmarks'].find { |b| b['id'] == bid }
      if match
        m = match['metrics'] || {}
        mem = m['memory_peak_kb'] || 0
        cu = m['cpu_user_ms'] || 0
        cs = m['cpu_sys_ms'] || 0
        cells << "<td>#{mem} KB</td>"
        cells << "<td>#{format('%.1f / %.1f ms', cu, cs)}</td>"
      else
        cells << '<td class="na">—</td><td class="na">—</td>'
      end
    end
    "<tr>#{cells.join}</tr>"
  end.join("\n")
  resource_table = <<~HTML
    <h2>Resource Usage</h2>
    <table>
      <tr><th>Benchmark</th>#{res_header}</tr>
      #{res_rows}
    </table>
  HTML

  # Chart data (one bar chart per benchmark shape)
  charts = all_ids.map do |bid|
    labels = []
    latency = []
    libraries.each do |lib|
      labels << lib['name']
      match = lib['benchmarks'].find { |b| b['id'] == bid }
      latency << (match ? match.dig('metrics', 'latency_us', 'min') : nil)
    end
    { 'id' => bid, 'labels' => labels, 'latency' => latency }
  end

  chart_divs = charts.each_with_index.map do |_c, i|
    "<div class=\"chart-box\"><canvas id=\"chart#{i}\"></canvas></div>"
  end.join

  chart_scripts = charts.each_with_index.map do |c, i|
    colors = ['rgba(88,166,255,0.7)', 'rgba(63,185,80,0.7)', 'rgba(248,81,73,0.7)']
    borders = ['rgba(88,166,255,1)', 'rgba(63,185,80,1)', 'rgba(248,81,73,1)']
    <<~JS
      new Chart(document.getElementById('chart#{i}'), {
        type: 'bar',
        data: {
          labels: #{c['labels'].to_json},
          datasets: [{
            label: 'latency µs (min)',
            data: #{c['latency'].to_json},
            backgroundColor: #{colors.to_json},
            borderColor: #{borders.to_json},
            borderWidth: 1
          }]
        },
        options: {
          responsive: true,
          plugins: { title: { display: true,
                              text: #{c['id'].to_json},
                              color: '#e6edf3' } },
          scales: { y: { beginAtZero: true,
                         ticks: { color: '#8b949e' },
                         grid: { color: '#21262d' } },
                    x: { ticks: { color: '#8b949e' } } }
        }
      });
    JS
  end.join("\n")

  chart_section = <<~HTML
    <h2>Latency Charts (lower is better)</h2>
    <div class="chart-container">
      <div class="chart-row">#{chart_divs}</div>
    </div>
  HTML

  lib_versions = libraries.map { |l| "#{ERB::Util.html_escape(l['name'])} #{ERB::Util.html_escape(l['version'])}" }.join(', ')
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
      * { box-sizing: border-box; margin: 0; padding: 0; }
      body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
             background: #0d1117; color: #e6edf3; padding: 24px; }
      h1 { color: #58a6ff; margin-bottom: 8px; }
      .meta { color: #8b949e; margin-bottom: 24px; font-size: 14px; }
      h2 { color: #79c0ff; margin: 32px 0 12px; font-size: 20px; }
      table { border-collapse: collapse; width: 100%; margin-bottom: 16px;
              background: #161b22; border-radius: 8px; overflow: hidden; }
      th { background: #21262d; color: #79c0ff; padding: 10px 14px;
           text-align: left; font-size: 13px; font-weight: 600;
           border-bottom: 1px solid #30363d; }
      td { padding: 8px 14px; border-bottom: 1px solid #21262d; font-size: 13px; }
      tr:hover td { background: #1c2128; }
      .winner { color: #3fb950; font-weight: 600; }
      .loser { color: #f85149; }
      .na { color: #484f58; }
      .chart-container { background: #161b22; border-radius: 8px;
                         padding: 16px; margin-bottom: 20px; }
      .chart-row { display: flex; gap: 16px; flex-wrap: wrap; }
      .chart-box { flex: 1; min-width: 320px; }
      footer { margin-top: 40px; color: #484f58; font-size: 12px; }
    </style>
    </head>
    <body>
    <h1>XML Library Benchmark Matrix</h1>
    <div class="meta">#{lib_versions} &middot; #{platform} &middot; #{ts}</div>
    #{summary_table}
    #{resource_table}
    #{chart_section}
    <footer>Generated by bench2html.rb from bench_matrix YAML output.</footer>
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
