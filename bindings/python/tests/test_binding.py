import pytest

from pyleptris import Document, LeptrisError

XML = """<?xml version="1.0"?>
<library>
  <book id="1" lang="en">Ulysses</book>
  <book id="2" lang="fr">L'Etranger</book>
  <!-- a comment -->
</library>"""


@pytest.fixture()
def doc():
    with Document.parse(XML) as doc:
        yield doc


class TestParse:
    def test_root_name(self, doc):
        assert doc.root.name == "library"

    def test_root_is_element(self, doc):
        assert doc.root.to_node().is_element()

    def test_parse_error_raises(self):
        with pytest.raises(LeptrisError):
            Document.parse("<unclosed>")

    def test_bytes_input(self):
        with Document.parse(b"<r/>") as doc:
            assert doc.root.name == "r"

    def test_type_error_on_non_string(self):
        with pytest.raises(TypeError):
            Document.parse(123)

    def test_close_is_idempotent(self):
        doc = Document.parse("<r/>")
        doc.close()
        doc.close()
        assert doc.closed


class TestElement:
    def test_child_iteration(self, doc):
        books = list(doc.root)
        assert [b.name for b in books] == ["book", "book"]

    def test_attribute(self, doc):
        book = doc.root.first_child_element
        assert book.attribute("id") == "1"
        assert book.attribute("lang") == "en"

    def test_attribute_default(self, doc):
        book = doc.root.first_child_element
        assert book.attribute("nope") is None
        assert book.attribute("nope", "x") == "x"

    def test_attributes_iteration(self, doc):
        book = doc.root.first_child_element
        pairs = list(book.attributes())
        assert pairs == [("id", "1"), ("lang", "en")]
        assert book.attribute_count == 2

    def test_attributes_entity_expansion_and_empty(self):
        with Document.parse('<e t="a &amp; b"/>') as doc:
            assert list(doc.root.attributes()) == [("t", "a & b")]
        with Document.parse("<e/>") as doc:
            assert list(doc.root.attributes()) == []
            assert doc.root.attribute_count == 0

    def test_text(self, doc):
        book = doc.root.first_child_element
        assert book.text == "Ulysses"

    def test_parent(self, doc):
        book = doc.root.first_child_element
        assert book.parent is not None
        assert book.parent.name == "library"

    def test_next_sibling(self, doc):
        first = doc.root.first_child_element
        second = first.next_sibling_element
        assert second is not None
        assert second.attribute("id") == "2"


class TestNode:
    def test_node_types(self, doc):
        node = doc.root.to_node()
        types = set()
        child = node.first_child
        while child is not None:
            types.add(child.type)
            child = child.next_sibling
        assert 1 in types  # text
        assert 2 in types  # comment

    def test_comment_content(self, doc):
        node = doc.root.to_node().first_child
        comments = []
        while node is not None:
            if node.is_comment():
                comments.append(node.content)
            node = node.next_sibling
        # Comment content is the exact inner text, whitespace included.
        assert comments == [" a comment "]

    def test_child_count_counts_elements(self, doc):
        # child_count is elements-only: root has 2 books plus
        # interleaved text nodes that are not counted.
        assert doc.root.to_node().child_count == 2


class TestXPath:
    def test_count(self, doc):
        assert doc.xpath("count(//book)") == 2.0

    def test_nodeset(self, doc):
        books = doc.xpath("//book")
        assert len(books) == 2
        assert books[0].text == "Ulysses"

    def test_string(self, doc):
        assert doc.xpath("string(//book[@id='2'])") == "L'Etranger"

    def test_boolean(self, doc):
        assert doc.xpath("count(//book) = 2") is True
        assert doc.xpath("count(//book) = 5") is False

    def test_element_context(self, doc):
        book = doc.root.first_child_element
        assert book.xpath("string(@id)") == "1"

    def test_error_raises(self, doc):
        with pytest.raises(LeptrisError):
            doc.xpath("///[")


class TestSerialize:
    def test_round_trip(self, doc):
        out = doc.serialize()
        assert "<library>" in out
        reparsed = Document.parse(out)
        assert reparsed.xpath("count(//book)") == 2.0
        reparsed.close()
