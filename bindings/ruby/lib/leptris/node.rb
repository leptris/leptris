# lib/leptris/node.rb — Leptris::Node wraps LeptrisNodeRef for generic
# tree traversal (all node types, not just elements).

module Leptris
  class Node
    attr_reader :ptr

    def initialize(ptr, document)
      @ptr = ptr
      @document = document
    end

    def type
      Leptris.leptris_node_get_type(@ptr)
    end

    def element?
      type == Leptris::NODE_ELEMENT
    end

    def text?
      type == Leptris::NODE_TEXT
    end

    def comment?
      type == Leptris::NODE_COMMENT
    end

    def cdata?
      type == Leptris::NODE_CDATA
    end

    def pi?
      type == Leptris::NODE_PI
    end

    def content
      case type
      when Leptris::NODE_TEXT     then Leptris.leptris_text_node_get_content(@ptr)
      when Leptris::NODE_COMMENT  then Leptris.leptris_comment_node_get_content(@ptr)
      when Leptris::NODE_CDATA    then Leptris.leptris_cdata_node_get_content(@ptr)
      else nil
      end
    end

    def first_child
      c = Leptris.leptris_node_first_child(@ptr)
      return nil if c.nil? || c.null?
      Node.new(c, @document)
    end

    def next_sibling
      s = Leptris.leptris_node_next_sibling(@ptr)
      return nil if s.nil? || s.null?
      Node.new(s, @document)
    end

    def child_count
      Leptris.leptris_node_child_count(@ptr)
    end

    def as_element
      return nil unless element?
      elem = Leptris.leptris_node_as_element(@ptr)
      return nil if elem.nil? || elem.null?
      Element.new(elem, @document)
    end

    def inspect
      "<Leptris::Node type=#{type}>"
    end
  end
end
