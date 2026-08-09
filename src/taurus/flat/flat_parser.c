/* flat/flat_parser.c — Flat XML parser implementation (TODO 139 Phase B).
 *
 * Single-pass XML scanner that builds a FlatDoc. See flat_parser.h
 * for the architectural rationale and scope.
 *
 * The parser maintains a stack of currently-open element indices.
 * When it sees `</name>`, it pops the top and verifies the name
 * matches (length first, then bytes — length mismatch short-circuits
 * the strcmp). When it sees `<name`, it pushes a new element.
 *
 * Hot path is element open + attribute scan. Each element is two
 * flat_doc_append_node calls (open at `<`, set edges at `>`/`/>`)
 * plus N flat_doc_append_attr calls. Each call is a single array
 * append + field init — no pool, no compact-pointer encode, no
 * namespace walk.
 */
#include "flat_parser.h"
#include "../common/chartype.h"

#include <string.h>
#include <stdint.h>

/* Hard cap on element nesting. Matches TAURUS_MAX_ELEMENT_DEPTH
 * in parser_new.h. Documents deeper than this are rejected; caller
 * falls back to the legacy parser which has the same cap. */
#define FLAT_MAX_DEPTH 256

typedef struct {
    const char* pos;
    const char* end;
    const char* xml_start;  /* For computing offsets into the input */
    int has_error;
    int standalone;         /* -1 = unset, 0 = no, 1 = yes */
    uint32_t version_offset;
    uint16_t version_len;
    uint32_t encoding_offset;
    uint16_t encoding_len;
    int saw_xml_decl;
    int has_bom;
    /* Source line counter (1-based). Issue #223: bumped as the
     * scanner crosses '\n' bytes; frozen into each FlatNode at
     * creation so flat_promote can copy it to TaurusNode.base.line. */
    uint32_t line;

    /* Open-element stack. The N-th entry is the FlatDoc node index
     * of the currently-open element at depth N. depth 0 = root. */
    uint32_t open_stack[FLAT_MAX_DEPTH];
    uint16_t open_name_len[FLAT_MAX_DEPTH];
    uint32_t open_name_offset[FLAT_MAX_DEPTH];
    /* Last child appended to each open element. Used to chain
     * siblings via flat_node_set_next_sibling. */
    uint32_t open_last_child[FLAT_MAX_DEPTH];
    int depth;
} FlatParser;

/* ============================================================================
 * Inline character classification — same conventions as parser_new.c
 * ============================================================================ */

/* Lookup tables for character classification (pugixml technique).
 *
 * Replaces 6 per-byte comparisons with a single array lookup. The
 * tables stay in L1 cache — they're only 256 bytes each.
 *
 * For XML name chars: a-z, A-Z, 0-9, '_', ':', '-', '.'
 * For XML name start: a-z, A-Z, '_', ':'
 * For whitespace: ' ', '\t', '\n', '\r' */
/* Character classification — uses the shared chartype table from
 * common/chartype.h (TODO 149 Phase 1). The table packs name-start,
 * name-char, and whitespace into bitflags so callers test multiple
 * properties in one indexed access.
 *
 * The name-start and name-char wrappers below preserve the UTF-8
 * multibyte fallback (bytes >= 0xC0 are accepted as potential
 * name chars; validation is deferred to the promote pass). */

static inline int fp_is_ws(char c) {
    return IS_WS(c);
}

static inline void fp_skip_ws(FlatParser* p) {
    while (p->pos < p->end && IS_WS(*p->pos)) {
        if (*p->pos == '\n') p->line++;
        p->pos++;
    }
}

/* Tally '\n' bytes in [from, to) and fold into p->line. Used after
 * bulk scans (memchr for text, multi-char literal matches). */
static inline void fp_advance_line(FlatParser* p,
                                    const char* from, const char* to) {
    for (const char* c = from; c < to; c++) {
        if (*c == '\n') p->line++;
    }
}

static inline int fp_is_name_start(unsigned char c) {
    if (c < 128) return IS_NAME_START(c);
    /* UTF-8 multibyte start. Validation deferred. */
    return c >= 0xC0;
}

static inline int fp_is_name_char(unsigned char c) {
    if (c < 128) return IS_NAME_CHAR(c);
    /* UTF-8 continuation or start bytes — accept as potential
     * name chars. The promote pass validates via utf8proc. */
    return c >= 0x80;
}

/* Scan one XML name. Returns length via *out_len and offset via
 * *out_offset. Returns 0 on success, -1 if no valid name start. */
static int fp_scan_name(FlatParser* p, uint32_t* out_offset, uint16_t* out_len) {
    if (p->pos >= p->end || !fp_is_name_start((unsigned char)*p->pos)) {
        return -1;
    }
    const char* start = p->pos;
    /* ASCII tight loop — fast enough for typical XML name lengths
     * (10-20 chars). SIMD scan was tried (TODO 144) but added
     * overhead without payoff; names are too short to amortize
     * the vector setup cost. */
    while (p->pos < p->end) {
        unsigned char c = (unsigned char)*p->pos;
        if (c >= 0x80) break;
        if (!fp_is_name_char(c)) break;
        p->pos++;
    }
    /* UTF-8 continuation — accept any high bytes. */
    while (p->pos < p->end) {
        unsigned char c = (unsigned char)*p->pos;
        if (c < 0x80) {
            if (!fp_is_name_char(c)) break;
            p->pos++;
        } else if (c >= 0xC0) {
            p->pos++;
            while (p->pos < p->end &&
                   ((unsigned char)*p->pos & 0xC0) == 0x80) {
                p->pos++;
            }
        } else {
            break;
        }
    }
    *out_offset = (uint32_t)(start - p->xml_start);
    *out_len = (uint16_t)(p->pos - start);
    return 0;
}

/* Skip a single- or double-quoted attribute value. Sets *out_offset
 * and *out_len to the raw bytes inside the quotes (no entity
 * expansion). Returns 0 on success, -1 on malformed input. */
static int fp_scan_quoted(FlatParser* p, uint32_t* out_offset, uint16_t* out_len) {
    if (p->pos >= p->end) return -1;
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') return -1;
    p->pos++;
    const char* start = p->pos;
    /* memchr would be overkill for typical short values; tight loop
     * scans until matching quote or end of input. */
    while (p->pos < p->end && *p->pos != quote) {
        p->pos++;
    }
    if (p->pos >= p->end) return -1;  /* unterminated */
    *out_offset = (uint32_t)(start - p->xml_start);
    *out_len = (uint16_t)(p->pos - start);
    p->pos++;  /* skip closing quote */
    return 0;
}

/* ============================================================================
 * Comment / CDATA / PI / DOCTYPE scanners
 *
 * These scan from just AFTER the `<` + marker sequence up to and
 * including the closing sequence. They return one FlatNode append
 * on success or -1 on malformed input.
 * ============================================================================ */

static int fp_scan_comment(FlatParser* p, FlatDoc* doc) {
    /* Assumes p->pos is at the `!` of `<!--`. */
    if (p->end - p->pos < 4 ||
        p->pos[1] != '!' || p->pos[2] != '-' || p->pos[3] != '-') {
        return -1;
    }
    uint32_t markup_line = p->line;
    const char* markup_start = p->pos;
    p->pos += 4;
    const char* start = p->pos;
    /* Find `-->`. */
    while (p->pos + 2 < p->end) {
        if (p->pos[0] == '-' && p->pos[1] == '-' && p->pos[2] == '>') {
            uint32_t text_offset = (uint32_t)(start - p->xml_start);
            uint32_t text_len = (uint32_t)(p->pos - start);
            uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_COMMENT, 0, 0);
            if (idx == FLAT_INDEX_NULL) return -1;
            flat_node_set_text(&doc->nodes[idx], text_offset, text_len);
            doc->nodes[idx].line = markup_line;
            p->pos += 3;
            fp_advance_line(p, markup_start, p->pos);
            return (int)idx;
        }
        p->pos++;
    }
    fp_advance_line(p, markup_start, p->pos);
    return -1;  /* unterminated comment */
}

static int fp_scan_cdata(FlatParser* p, FlatDoc* doc) {
    /* Verify `<![CDATA[`. */
    static const char kCdataMarker[] = "![CDATA[";
    if (p->end - p->pos < (ptrdiff_t)sizeof(kCdataMarker) - 1 + 3 /* ]]> */ ||
        memcmp(p->pos + 1, kCdataMarker, sizeof(kCdataMarker) - 1) != 0) {
        return -1;
    }
    uint32_t markup_line = p->line;
    const char* markup_start = p->pos;
    p->pos += sizeof(kCdataMarker);  /* past `<![CDATA[` */
    const char* start = p->pos;
    /* Find `]]>`. */
    while (p->pos + 2 < p->end) {
        if (p->pos[0] == ']' && p->pos[1] == ']' && p->pos[2] == '>') {
            uint32_t text_offset = (uint32_t)(start - p->xml_start);
            uint32_t text_len = (uint32_t)(p->pos - start);
            uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_CDATA, 0, 0);
            if (idx == FLAT_INDEX_NULL) return -1;
            flat_node_set_text(&doc->nodes[idx], text_offset, text_len);
            doc->nodes[idx].line = markup_line;
            p->pos += 3;
            fp_advance_line(p, markup_start, p->pos);
            return (int)idx;
        }
        p->pos++;
    }
    fp_advance_line(p, markup_start, p->pos);
    return -1;
}

static int fp_scan_pi(FlatParser* p, FlatDoc* doc) {
    /* Assumes p->pos is at the `?` of `<?`. */
    if (p->pos[1] != '?') return -1;
    uint32_t pi_line = p->line;
    const char* pi_start = p->pos;
    p->pos += 2;
    /* PI target = XML name. */
    uint32_t target_offset;
    uint16_t target_len;
    if (fp_scan_name(p, &target_offset, &target_len) != 0) return -1;

    /* Skip whitespace before data. The serializer inserts its own
     * space between target and data, so we strip leading whitespace
     * here to match the legacy parser's behavior. */
    fp_skip_ws(p);
    const char* data_start = p->pos;
    while (p->pos < p->end && *p->pos != '?') p->pos++;
    if (p->pos >= p->end || p->pos + 1 >= p->end ||
        p->pos[0] != '?' || p->pos[1] != '>') return -1;
    uint32_t data_offset = (uint32_t)(data_start - p->xml_start);
    uint32_t data_len = (uint32_t)(p->pos - data_start);

    /* XML declaration `<?xml ...?>` is special-cased: the version,
     * encoding, and standalone attributes are recorded on the doc
     * and NO node is appended. Promote-time the document reflects
     * these without needing a separate node. */
    if (target_len == 3 &&
        memcmp(p->xml_start + target_offset, "xml", 3) == 0) {
        /* Parse the pseudo-attributes inside data_offset..data_len. */
        const char* saved_pos = p->pos;
        const char* saved_end = p->end;
        p->pos = p->xml_start + data_offset;
        p->end = p->xml_start + data_offset + data_len;
        for (;;) {
            fp_skip_ws(p);
            if (p->pos >= p->end) break;
            uint32_t a_name_off;
            uint16_t a_name_len;
            if (fp_scan_name(p, &a_name_off, &a_name_len) != 0) break;
            fp_skip_ws(p);
            if (p->pos >= p->end || *p->pos != '=') break;
            p->pos++;
            fp_skip_ws(p);
            uint32_t v_off;
            uint16_t v_len;
            if (fp_scan_quoted(p, &v_off, &v_len) != 0) break;
            if (a_name_len == 7 &&
                memcmp(p->xml_start + a_name_off, "version", 7) == 0) {
                p->version_offset = v_off;
                p->version_len = v_len;
            } else if (a_name_len == 8 &&
                       memcmp(p->xml_start + a_name_off, "encoding", 8) == 0) {
                p->encoding_offset = v_off;
                p->encoding_len = v_len;
            } else if (a_name_len == 10 &&
                       memcmp(p->xml_start + a_name_off, "standalone", 10) == 0) {
                const char* v = p->xml_start + v_off;
                if (v_len == 3 && memcmp(v, "yes", 3) == 0) p->standalone = 1;
                else if (v_len == 2 && memcmp(v, "no", 2) == 0) p->standalone = 0;
            }
        }
        p->pos = saved_pos;
        p->end = saved_end;
        p->saw_xml_decl = 1;
        p->pos += 2;  /* skip `?>` */
        fp_advance_line(p, pi_start, p->pos);
        return -2;  /* sentinel: handled, no node */
    }

    /* Regular PI. The target lives in name_offset/name_len; data
     * lives in the text slot via the overload helper. */
    uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_PI,
                                         target_offset, target_len);
    if (idx == FLAT_INDEX_NULL) return -1;
    flat_node_set_pi_data(&doc->nodes[idx], data_offset, data_len);
    doc->nodes[idx].line = pi_line;
    p->pos += 2;  /* skip `?>` */
    fp_advance_line(p, pi_start, p->pos);
    return (int)idx;
}

static int fp_skip_doctype(FlatParser* p) {
    /* Verify `<!DOCTYPE`. */
    static const char kDoctype[] = "!DOCTYPE";
    if (p->end - p->pos < (ptrdiff_t)sizeof(kDoctype) - 1 ||
        memcmp(p->pos + 1, kDoctype, sizeof(kDoctype) - 1) != 0) {
        return -1;
    }
    p->pos += sizeof(kDoctype);  /* past `<!DOCTYPE` */

    /* Scan to the matching `>`, respecting `[ ... ]` internal subset
     * blocks. The internal subset can contain nested `<` `>` inside
     * entity values, so bracket-counting is required. */
    int bracket_depth = 0;
    while (p->pos < p->end) {
        char c = *p->pos++;
        if (c == '[') bracket_depth++;
        else if (c == ']') {
            if (bracket_depth > 0) bracket_depth--;
        } else if (c == '>' && bracket_depth == 0) {
            return 0;
        }
    }
    return -1;  /* unterminated */
}

/* ============================================================================
 * Element scanning
 *
 * fp_scan_element_start is called with p->pos at the `<`. It scans
 * the start tag (including attributes), appends an element node and
 * N attribute nodes, links the element into its parent's child list
 * (via the open_stack), and pushes the new element index onto the
 * open_stack if it has children (not self-closing).
 *
 * Returns:
 *   >=0  FlatNode index of the new element
 *   -1   hard parse failure
 * ============================================================================ */

static int fp_scan_element_start(FlatParser* p, FlatDoc* doc) {
    if (p->pos >= p->end || *p->pos != '<') return -1;
    /* Snapshot the line where `<` appeared. Issue #223. */
    uint32_t elem_line = p->line;
    const char* elem_start = p->pos;
    p->pos++;

    uint32_t name_offset;
    uint16_t name_len;
    if (fp_scan_name(p, &name_offset, &name_len) != 0) return -1;

    if (p->depth >= FLAT_MAX_DEPTH) return -1;

    /* Snapshot the attr_start BEFORE appending attrs so we can
     * record (attr_start, attr_count) on the element. */
    uint32_t attr_start = (uint32_t)doc->attr_count;
    uint32_t attrs_appended = 0;

    /* Attribute scan loop. Terminates on `>` (open) or `/>` (close). */
    int self_closing = 0;
    for (;;) {
        fp_skip_ws(p);
        if (p->pos >= p->end) return -1;
        char c = *p->pos;
        if (c == '>') {
            p->pos++;
            break;
        }
        if (c == '/') {
            if (p->pos + 1 >= p->end || p->pos[1] != '>') return -1;
            self_closing = 1;
            p->pos += 2;
            break;
        }

        /* Attribute: name `=` quote value quote */
        uint32_t a_name_off;
        uint16_t a_name_len;
        if (fp_scan_name(p, &a_name_off, &a_name_len) != 0) return -1;
        fp_skip_ws(p);
        if (p->pos >= p->end || *p->pos != '=') return -1;
        p->pos++;
        fp_skip_ws(p);
        uint32_t v_off;
        uint16_t v_len;
        if (fp_scan_quoted(p, &v_off, &v_len) != 0) return -1;

        if (flat_doc_append_attr(doc, a_name_off, a_name_len,
                                  v_off, v_len) == FLAT_INDEX_NULL) {
            return -1;
        }
        attrs_appended++;
    }

    /* Append the element node. */
    uint32_t elem_idx = flat_doc_append_node(doc, FLAT_NODE_ELEMENT,
                                              name_offset, name_len);
    if (elem_idx == FLAT_INDEX_NULL) return -1;
    flat_node_set_attrs(&doc->nodes[elem_idx], attr_start,
                         (uint16_t)attrs_appended);
    flat_node_set_depth(&doc->nodes[elem_idx], (uint16_t)p->depth);
    doc->nodes[elem_idx].line = elem_line;
    /* Fold any newlines crossed while scanning the open tag into
     * p->line so the next sibling/child starts on the right line. */
    fp_advance_line(p, elem_start, p->pos);

    /* Link into parent. */
    if (p->depth > 0) {
        uint32_t parent_idx = p->open_stack[p->depth - 1];
        flat_node_set_parent(&doc->nodes[elem_idx], parent_idx);
        uint32_t prev_last = p->open_last_child[p->depth - 1];
        if (prev_last == FLAT_INDEX_NULL) {
            flat_node_set_first_child(&doc->nodes[parent_idx], elem_idx);
        } else {
            flat_node_set_next_sibling(&doc->nodes[prev_last], elem_idx);
        }
        p->open_last_child[p->depth - 1] = elem_idx;
    } else {
        /* Root. Record on the doc. */
        if (doc->root_index == FLAT_INDEX_NULL) {
            doc->root_index = elem_idx;
        }
        /* Multiple top-level elements: malformed XML; bail. */
        else return -1;
    }

    /* Push onto open_stack if not self-closing. */
    if (!self_closing) {
        p->open_stack[p->depth] = elem_idx;
        p->open_name_offset[p->depth] = name_offset;
        p->open_name_len[p->depth] = name_len;
        p->open_last_child[p->depth] = FLAT_INDEX_NULL;
        p->depth++;
    }

    return (int)elem_idx;
}

/* Scan `</name>` and pop the matching element from open_stack.
 * Returns 0 on success, -1 on mismatch / underflow. */
static int fp_scan_element_end(FlatParser* p, const FlatDoc* doc) {
    if (p->end - p->pos < 2 || p->pos[1] != '/') return -1;
    p->pos += 2;

    uint32_t name_offset;
    uint16_t name_len;
    if (fp_scan_name(p, &name_offset, &name_len) != 0) return -1;
    fp_skip_ws(p);
    if (p->pos >= p->end || *p->pos != '>') return -1;
    p->pos++;

    if (p->depth == 0) return -1;  /* nothing to close */
    uint16_t open_len = p->open_name_len[p->depth - 1];
    uint32_t open_off = p->open_name_offset[p->depth - 1];
    if (open_len != name_len) return -1;
    if (memcmp(doc->xml_buffer + open_off,
               doc->xml_buffer + name_offset,
               name_len) != 0) {
        return -1;
    }
    p->depth--;
    return 0;
}

/* Append a child node (comment / cdata / pi / text) to the current
 * open element. Used after the per-construct scanners to maintain
 * sibling chains. */
static void fp_link_child(FlatParser* p, FlatDoc* doc, uint32_t child_idx) {
    if (p->depth == 0) return;  /* top-level non-element — fine */
    uint32_t parent_idx = p->open_stack[p->depth - 1];
    flat_node_set_parent(&doc->nodes[child_idx], parent_idx);
    uint32_t prev_last = p->open_last_child[p->depth - 1];
    if (prev_last == FLAT_INDEX_NULL) {
        flat_node_set_first_child(&doc->nodes[parent_idx], child_idx);
    } else {
        flat_node_set_next_sibling(&doc->nodes[prev_last], child_idx);
    }
    p->open_last_child[p->depth - 1] = child_idx;
}

/* ============================================================================
 * Public entry point
 * ============================================================================ */

FlatDoc* flat_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    FlatDoc* doc = flat_doc_new(xml, len);
    if (!doc) return NULL;

    FlatParser p;
    p.pos = xml;
    p.end = xml + len;
    p.xml_start = xml;
    p.has_error = 0;
    p.standalone = -1;
    p.version_offset = 0;
    p.version_len = 0;
    p.encoding_offset = 0;
    p.encoding_len = 0;
    p.saw_xml_decl = 0;
    p.has_bom = 0;
    p.depth = 0;
    p.line = 1;

    /* BOM. */
    if (len >= 3 &&
        (unsigned char)xml[0] == 0xEF &&
        (unsigned char)xml[1] == 0xBB &&
        (unsigned char)xml[2] == 0xBF) {
        p.pos += 3;
        p.has_bom = 1;
    }

    /* Main loop. */
    int root_seen = 0;
    while (p.pos < p.end) {
        /* Skip inter-element whitespace at the top level. Inside
         * elements it becomes a text node. */
        if (p.depth == 0) {
            fp_skip_ws(&p);
            if (p.pos >= p.end) break;
        }

        if (*p.pos != '<') {
            /* Text content. Use memchr to find the next '<' —
             * libc's memchr is often vectorized and processes
             * 16+ bytes per cycle. */
            const char* text_start = p.pos;
            const char* lt = (const char*)memchr(p.pos, '<', p.end - p.pos);
            p.pos = lt ? lt : p.end;
            size_t len = p.pos - text_start;
            /* Snapshot the line where the text started; advance the
             * counter across the consumed range so the NEXT token
             * starts on the correct line. Issue #223. */
            uint32_t text_line = p.line;
            fp_advance_line(&p, text_start, p.pos);
            if (len == 0) continue;

            /* Text at the document level (no open element) is only
             * legal as whitespace separating the root from comments
             * / PIs / DOCTYPE. Non-whitespace text outside the
             * document element is malformed XML. */
            if (p.depth == 0) {
                for (const char* c = text_start; c < p.pos; c++) {
                    if (!fp_is_ws(*c)) goto fail;
                }
                continue;  /* whitespace-only, drop it */
            }

            uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_TEXT, 0, 0);
            if (idx == FLAT_INDEX_NULL) goto fail;
            flat_node_set_text(&doc->nodes[idx],
                                (uint32_t)(text_start - p.xml_start),
                                (uint32_t)len);
            doc->nodes[idx].line = text_line;
            fp_link_child(&p, doc, idx);
            continue;
        }

        /* `<` — dispatch on what follows. */
        if (p.end - p.pos < 2) goto fail;  /* lone `<` */
        char next = p.pos[1];

        if (next == '!') {
            /* Comment, CDATA, or DOCTYPE. */
            if (p.end - p.pos >= 4 &&
                p.pos[2] == '-' && p.pos[3] == '-') {
                int r = fp_scan_comment(&p, doc);
                if (r < 0) goto fail;
                fp_link_child(&p, doc, (uint32_t)r);
            } else if (p.end - p.pos >= 9 &&
                       memcmp(p.pos + 2, "[CDATA[", 7) == 0) {
                int r = fp_scan_cdata(&p, doc);
                if (r < 0) goto fail;
                fp_link_child(&p, doc, (uint32_t)r);
            } else {
                if (fp_skip_doctype(&p) != 0) goto fail;
            }
            continue;
        }

        if (next == '?') {
            int r = fp_scan_pi(&p, doc);
            if (r == -1) goto fail;
            if (r == -2) continue;  /* xml decl handled */
            if (p.depth > 0 || root_seen) {
                fp_link_child(&p, doc, (uint32_t)r);
            }
            continue;
        }

        if (next == '/') {
            if (fp_scan_element_end(&p, doc) != 0) goto fail;
            if (p.depth == 0) {
                /* Just closed the root. Remaining content can only
                 * be whitespace, comments, or PIs. */
                root_seen = 1;
            }
            continue;
        }

        /* Element start. */
        int r = fp_scan_element_start(&p, doc);
        if (r < 0) goto fail;
        root_seen = 1;
    }

    /* Unclosed elements at end of input = malformed. */
    if (p.depth != 0) goto fail;

    /* No root element = malformed. */
    if (doc->root_index == FLAT_INDEX_NULL) goto fail;

    /* Commit parser state to the doc. */
    doc->version_offset = p.version_offset;
    doc->version_len = p.version_len;
    doc->encoding_offset = p.encoding_offset;
    doc->encoding_len = p.encoding_len;
    doc->standalone = p.standalone;

    return doc;

fail:
    flat_doc_free(doc);
    return NULL;
}
