require 'spec_helper'

RSpec.describe Leptris::Document do
  let(:xml) { '<root><child id="1">hello</child><child id="2">world</child></root>' }

  describe '.parse' do
    it 'parses valid XML' do
      doc = described_class.parse(xml)
      expect(doc).not_to be_nil
      expect(doc.freed?).to be(false)
      doc.free
    end

    it 'raises on invalid XML' do
      expect { described_class.parse('<broken>') }.to raise_error(Leptris::Error)
    end
  end

  describe '#root' do
    it 'returns the root element' do
      doc = described_class.parse(xml)
      root = doc.root
      expect(root.name).to eq('root')
      doc.free
    end
  end

  describe '#xpath' do
    it 'evaluates count()' do
      doc = described_class.parse(xml)
      expect(doc.xpath('count(//child)')).to eq(2.0)
      doc.free
    end

    it 'evaluates string literal' do
      doc = described_class.parse(xml)
      expect(doc.xpath("'hello'")).to eq('hello')
      doc.free
    end

    it 'evaluates number literal' do
      doc = described_class.parse(xml)
      expect(doc.xpath('42')).to eq(42.0)
      doc.free
    end

    it 'returns nodeset as Element array' do
      doc = described_class.parse(xml)
      results = doc.xpath('//child')
      expect(results).to be_an(Array)
      expect(results.length).to eq(2)
      expect(results[0]).to be_a(Leptris::Element)
      expect(results[0]['id']).to eq('1')
      expect(results[1]['id']).to eq('2')
      doc.free
    end
  end

  describe '#serialize' do
    it 'round-trips XML' do
      doc = described_class.parse(xml)
      serialized = doc.serialize
      expect(serialized).to include('<root>')
      expect(serialized).to include('hello')
      doc.free
    end
  end
end

RSpec.describe Leptris::Element do
  let(:doc) { Leptris::Document.parse('<root attr="val"><a>text</a></root>') }
  let(:root) { doc.root }

  after { doc.free }

  it 'returns name' do
    expect(root.name).to eq('root')
  end

  it 'returns text content' do
    expect(root.text).to eq('text')
  end

  it 'returns attributes' do
    expect(root['attr']).to eq('val')
  end

  it 'iterates children' do
    children = root.each_child_element.to_a
    expect(children.length).to eq(1)
    expect(children[0].name).to eq('a')
  end
end
