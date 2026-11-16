#!/usr/bin/env ruby
# Frozen output = false
# The B3 pipeline-shape corpora (SOTA #2055): four FIXED synthetic
# documents, one per consumer shape, committed alongside this
# generator for provenance. Re-running reproduces them byte-for-byte
# (deterministic LCG, no srand(Env)).
#   pipeline_metanorma.xml — standards document: sections, terms,
#       references (the metanorma compile shape)
#   pipeline_config.xml    — services config tree (the validation shape)
#   pipeline_feed.xml      — syndication feed with entries (the
#       normalization shape)
#   pipeline_records.xml   — flat records with status (the
#       extract-transform shape)

# Deterministic LCG so every run writes identical bytes.
class Lcg
  def initialize(seed)
    @s = seed
  end

  def rnd
    @s = (@s * 1_103_515_245 + 12_345) & 0x7fff_ffff
    @s
  end

  def word(n)
    cons = %w[b c d f g h j k l m n p r s t v]
    vows = %w[a e i o u]
    w = (0...n).map { |i| i.even? ? cons[rnd % cons.size] : vows[rnd % vows.size] }.join
    w
  end

  def pick(arr)
    arr[rnd % arr.size]
  end
end

def write(name, body)
  File.write(File.join(__dir__, name), body)
  puts "#{name}: #{body.bytesize} bytes"
end

# ---- metanorma shape -------------------------------------------------
rng = Lcg.new(2026_11_16)
out = +<<~HDR
  <?xml version="1.0" encoding="UTF-8"?>
  <!-- B3 fixed corpus (SOTA #2055): the metanorma-compile shape. -->
  <standard version="1.0">
HDR
(1..12).each do |c|
  out << "  <clause id=\"clause-#{c}\"><title>Section #{rng.word(6)} #{c}</title>\n"
  out << "    <terms>\n"
  (1..12).each do |t|
    term = rng.word(5)
    out << "      <term><preferred>#{term}</preferred>" \
           "<domain>#{rng.word(4)}</domain>" \
           "<definition>#{rng.word(7)} #{rng.word(5)} #{rng.word(4)}.</definition></term>\n"
  end
  out << "    </terms>\n    <references>\n"
  (1..4).each do |r|
    out << "      <ref bibitem=\"B#{c}-#{r}\">#{rng.word(8)} #{r}</ref>\n"
  end
  out << "    </references>\n  </clause>\n"
end
out << "</standard>\n"
write("pipeline_metanorma.xml", out)

# ---- config shape ----------------------------------------------------
rng = Lcg.new(777_001)
hosts = %w[web api db cache queue mail cdn auth metrics]
out = +<<~HDR
  <?xml version="1.0" encoding="UTF-8"?>
  <!-- B3 fixed corpus (SOTA #2055): the config-validation shape. -->
  <config env="prod">
    <defaults timeout="30" retries="3"/>
    <services>
HDR
(1..96).each do |s|
  host = rng.pick(hosts)
  tls = s.even? ? " tls=\"true\"" : ""
  out << "      <service name=\"#{host}#{s}\" host=\"#{host}.svc\"" \
         " port=\"#{8000 + rng.rnd % 900}\"#{tls}>" \
         "<route path=\"/#{rng.word(4)}/#{s}\"/></service>\n"
end
out << "    </services>\n  </config>\n"
write("pipeline_config.xml", out)

# ---- feed shape ------------------------------------------------------
rng = Lcg.new(424_242)
out = +<<~HDR
  <?xml version="1.0" encoding="UTF-8"?>
  <!-- B3 fixed corpus (SOTA #2055): the feed-normalization shape. -->
  <feed>
    <title>the pipeline fixture feed</title>
    <id>urn:leptris:pipeline:feed</id>
HDR
(1..160).each do |e|
  out << "    <entry id=\"e#{e}\"><title>#{rng.word(6)} #{rng.word(4)} #{e}</title>" \
         "<link href=\"https://example.test/#{rng.word(5)}/#{e}\"/>" \
         "<summary>#{rng.word(9)} #{rng.word(6)}.</summary>" \
         "<category term=\"#{rng.word(4)}\"/></entry>\n"
end
out << "  </feed>\n"
write("pipeline_feed.xml", out)

# ---- records shape ---------------------------------------------------
rng = Lcg.new(9_876_543)
statuses = %w[active active active pending archived]
out = +<<~HDR
  <?xml version="1.0" encoding="UTF-8"?>
  <!-- B3 fixed corpus (SOTA #2055): the extract-transform shape. -->
  <records>
HDR
(1..280).each do |r|
  out << "    <record id=\"r#{r}\" status=\"#{rng.pick(statuses)}\"" \
         " amount=\"#{(rng.rnd % 100_000) / 100.0}\">" \
         "#{rng.word(6)}</record>\n"
end
out << "  </records>\n"
write("pipeline_records.xml", out)
