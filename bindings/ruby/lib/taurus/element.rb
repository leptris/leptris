# lib/taurus/element.rb — Taurus::Element wraps TaurusElement.
#
# Elements are owned by their parent Document.  They are never freed
# directly — freeing the Document frees the entire tree.

module Taurus
  class Element
    attr_reader :ptr

    def initialize(ptr, document)
      @ptr = ptr
      @document = document
    end

    def name
      Taurus.taurus_element_name(@ptr)
    end

    def text
      Taurus.taurus_element_text(@ptr)
    end

    def [](attr_name)
      Taurus.taurus_element_attribute(@ptr, attr_name, nil)
    end

    def attribute_count
      Taurus.taurus_element_attribute_count(@ptr)
    end

    def parent
      p = Taurus.taurus_element_parent(@ptr)
      return nil if p.nil? || p.null?
      Element.new(p, @document)
    end

    def first_child_element
      c = Taurus.taurus_element_first_child_any(@ptr)
      return nil if c.nil? || c.null?
      Element.new(c, @document)
    end

    def child_count
      Taurus.taurus_element_child_count(@ptr)
    end

    def each_child_element
      return enum_for(:each_child_element) unless block_given?
      child = first_child_element
      while child
        yield child
        child = child.next_sibling_element
      end
    end

    def next_sibling_element
      node = Taurus.taurus_element_as_node(@ptr)
      sibling = Taurus.taurus_node_next_sibling(node)
      return nil if sibling.nil? || sibling.null?
      elem = Taurus.taurus_node_as_element(sibling)
      return nil if elem.nil? || elem.null?
      Element.new(elem, @document)
    end

    def to_node
      Node.new(Taurus.taurus_element_as_node(@ptr), @document)
    end

    def inspect
      "<Taurus::Element #{name}>"
    end
  end
end
