require 'spec_helper'

RSpec.describe Leptris::SAX do
  let(:xml) do
    <<~XML
      <?xml version="1.0"?>
      <?stylesheet medium="print"?>
      <!-- doc header -->
      <library xmlns:dc="http://purl.org/dc/elements/1.1/">
        <book id="b1" dc:lang="en" tag="r&amp;d"><title>Résumé &amp; Career</title></book>
        <raw><![CDATA[x < y]]></raw>
      </library>
    XML
  end

  def events_for(input, handlers = nil)
    events = []
    handlers ||= {
      start_document: -> { events << [:start_document] },
      end_document: -> { events << [:end_document] },
      start_element: ->(n, a) { events << [:start_element, n, a] },
      end_element: ->(n) { events << [:end_element, n] },
      characters: ->(t) { events << [:characters, t] },
      comment: ->(t) { events << [:comment, t] },
      cdata: ->(t) { events << [:cdata, t] },
      processing_instruction: ->(t, d) { events << [:pi, t, d] },
      start_prefix_mapping: ->(p, u) { events << [:start_prefix, p, u] },
      end_prefix_mapping: ->(p) { events << [:end_prefix, p] }
    }
    described_class.parse(input, handlers)
    events
  end

  describe '.parse' do
    it 'fires document start and end' do
      events = events_for(xml)
      expect(events.first).to eq([:start_document])
      expect(events.last).to eq([:end_document])
    end

    it 'fires start_element with a name and attribute hash' do
      events = events_for(xml)
      book = events.find { |e| e[1] == 'book' }
      # Attribute values arrive entity-expanded (XML 1.0 3.3.3).
      expect(book[2]).to include('id' => 'b1', 'dc:lang' => 'en', 'tag' => 'r&d')
    end

    it 'hands out UTF-8 strings' do
      events = events_for(xml)
      title = events.find { |e| e[0] == :characters && e[1].include?('Résumé') }
      expect(title[1].encoding).to eq(Encoding::UTF_8)
      # Character data arrives entity-expanded (XML 1.0 2.4).
      expect(title[1]).to eq('Résumé & Career')
    end

    it 'fires comment, cdata, and processing instruction events' do
      events = events_for(xml)
      expect(events).to include([:comment, ' doc header '])
      expect(events).to include([:cdata, 'x < y'])
      # The XML declaration is not a PI; real PIs fire with
      # target + data.
      expect(events).to include([:pi, 'stylesheet', 'medium="print"'])
    end

    it 'fires prefix mapping around the declaring element' do
      events = events_for(xml)
      expect(events).to include([:start_prefix, 'dc',
                                  'http://purl.org/dc/elements/1.1/'])
      expect(events).to include([:end_prefix, 'dc'])
    end

    it 'returns true on success' do
      expect(described_class.parse('<a/>', {})).to be(true)
    end

    it 'raises on malformed input' do
      expect { described_class.parse('<a>', {}) }
        .to raise_error(Leptris::Error)
    end

    it 'reports failures through the :error handler before raising' do
      seen = []
      expect do
        described_class.parse('<a><b></a></b>', error: ->(m, l, c) { seen << [m, l, c] })
      end.to raise_error(Leptris::Error)
      expect(seen).not_to be_empty
    end

    it 'rejects non-String input' do
      expect { described_class.parse(42, {}) }.to raise_error(ArgumentError)
    end
  end

  describe Leptris::SAX::Parser do
    it 'parses a document fed in chunks (streaming mode)' do
      events = []
      parser = described_class.new(
        {
          start_element: ->(n, _a) { events << n },
          end_element: ->(n) { events << "/#{n}" },
          characters: ->(t) { events << t }
        },
        streaming: true
      )
      begin
        # Split points deliberately cut mid-tag and mid-text: the
        # text node spans a chunk boundary, so characters fires
        # once per chunk (SAX contract — concatenate to coalesce).
        xml = '<r><i v="1">hello wörld</i><i v="2"/></r>'
        parser.feed(xml[0, 9])
        parser.feed(xml[9, 8])
        parser.feed(xml[17, 100], final: true)
      ensure
        parser.free
      end
      expect(events).to eq(%w[r i hello] + [' wörld'] + %w[/i i /i /r])
    end

    it 'parses buffered chunks on the final feed (legacy mode)' do
      events = []
      parser = described_class.new({ start_element: ->(n, _a) { events << n } })
      begin
        parser.feed('<ro')
        parser.feed('ot><a/><')
        parser.feed('b/></root>', final: true)
      ensure
        parser.free
      end
      expect(events).to eq(%w[root a b])
    end

    it 'rejects non-String chunks' do
      parser = described_class.new({})
      begin
        expect { parser.feed(:nope) }.to raise_error(ArgumentError)
      ensure
        parser.free
      end
    end
  end
end
