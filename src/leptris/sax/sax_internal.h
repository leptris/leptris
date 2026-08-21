/* lib/src/sax/sax_internal.h
 *
 * Internal SAX parser layout.  Both the recursive parser
 * (parser.c) and the streaming state machine (streaming.c) need
 * the same struct; keeping it here avoids re-declaration drift
 * and gives the streaming implementation a clean place to add
 * fields without touching the existing parser.
 *
 * Public consumers never see this header.
 */
#ifndef LEPTRIS_SAX_INTERNAL_H
#define LEPTRIS_SAX_INTERNAL_H

#include "../../include/leptris/sax/sax.h"
#include <stddef.h>

/* Hard cap on element nesting for the explicit element-name stack
 * (TODO 116).  Same number as the recursive parser's depth guard.
 * At ~32 B per frame + name + attrs this caps streaming memory at
 * ~8 KB for the stack alone. */
#ifndef LEPTRIS_SAX_MAX_DEPTH
#define LEPTRIS_SAX_MAX_DEPTH 256
#endif

/* One element frame on the explicit nesting stack (TODO 116). */
#define SAX_NAME_INLINE 48u   /* names <= 47 bytes: no allocation */
#define SAX_ATTRS_INLINE 12u  /* up to 6 attributes inline */

typedef struct SaxElementFrame {
    /* Name storage: inline for the common short-name case (round 14:
     * the per-element malloc+free this replaces measured ~10% of SAX
     * parse, not the "under 1%" the old comment claimed). Long names
     * take name_heap; either way `name` points at valid storage for
     * the element's LIFO lifetime. */
    char   name_buf[SAX_NAME_INLINE];
    char*  name_heap;      /* NULL unless name_len >= SAX_NAME_INLINE */
    char*  name;           /* name_buf or name_heap, NUL-terminated */
    size_t name_len;
    /* Flat array of [name, value, name, value, ...] terminated by NULL.
     * Value pointers are scratch-owned; the ARRAY is inline for up to
     * SAX_ATTRS_INLINE entries, heap beyond. */
    char*  attrs_inline[SAX_ATTRS_INLINE];
    char** attrs_heap;     /* NULL while the inline array suffices */
    char** attrs;          /* attrs_inline or attrs_heap */
    size_t attr_count;
    size_t attr_cap;
    int    self_closing;   /* Already emitted start+end (empty element). */
} SaxElementFrame;

/* Streaming state machine states (TODO 116).  See streaming.c for
 * the transition table and per-state contracts. */
typedef enum {
    SAX_ST_TOPLEVEL,         /* Pre-root: skipping XML decl/DOCTYPE/misc. */
    SAX_ST_ELEM_OPEN_NAME,   /* Just past '<', scanning element name. */
    SAX_ST_ATTR_LIST,        /* Inside opening tag: attrs / whitespace / '>'. */
    SAX_ST_ATTR_NAME,        /* Scanning attribute name. */
    SAX_ST_ATTR_EQ,          /* Between name and '=', skipping whitespace. */
    SAX_ST_ATTR_VALUE_QUOTE, /* Past '=', skipping whitespace, expecting quote. */
    SAX_ST_ATTR_VALUE,       /* Inside quoted attribute value. */
    SAX_ST_ELEM_CONTENT,     /* Between '>' and '</tag>': dispatch text/child. */
    SAX_ST_TEXT,             /* Scanning text content, emitting characters(). */
    SAX_ST_CLOSING_TAG,      /* Inside '</tag>'. */
    SAX_ST_COMMENT,          /* Inside '<!-- ... -->'. */
    SAX_ST_CDATA,            /* Inside '<![CDATA[ ... ]]>'. */
    SAX_ST_PI,               /* Inside '<?...?>'. */
    SAX_ST_DONE,
    SAX_ST_ERROR
} SaxState;

/* Internal parser layout.  Shared by parser.c (legacy one-shot path)
 * and streaming.c (TODO 116 state machine).  Fields are documented
 * where the meaning is non-obvious. */
struct LeptrisSAXParser {
    /* Handler + scratch arena.  These are the "hot" members used on
     * every callback -- keep them at the front so they share a cache
     * line. */
    LeptrisSAXHandler* handler;
    void* user_data;

    /* Stream view: pos, end.  For legacy one-shot this is the
     * complete input; for streaming feeds it's [start, accum_end). */
    const char* pos;
    const char* end;
    int line;
    int column;
    int has_error;
    char error_message[256];

    /* Scratch arena (TODO 102).  Growable; reset at the start of
     * legacy one-shot, persisted across feed() calls in the
     * streaming path so element-name pointers remain valid until
     * the matching end tag. */
    char*  scratch;
    size_t scratch_len;
    size_t scratch_cap;

    /* Legacy incremental buffering (TODO 89).  When streaming == 0,
     * feed() accumulates here and parses once is_final is set. */
    char*  input_buf;
    size_t input_len;
    size_t input_cap;
    int    document_started;
    int    document_ended;

    /* ----- TODO 116 streaming additions ----- */

    /* Explicit state.  SAVED across feed() calls so the parser can
     * be resumed mid-token. */
    SaxState state;

    /* Explicit element-name stack.  Bounded by LEPTRIS_SAX_MAX_DEPTH. */
    SaxElementFrame elem_stack[LEPTRIS_SAX_MAX_DEPTH];
    size_t elem_depth;

    /* Per-token carry-over.  When a token (attr name, attr value,
     * comment, CDATA, PI target/data, closing tag name) might
     * straddle a chunk boundary, the partial bytes are buffered
     * here.  Cleared when the token completes. */
    char*  carry;
    size_t carry_len;
    size_t carry_cap;

    /* How many bytes of input_buf have already been scanned.  On
     * each feed(), new bytes are appended to input_buf, then
     * pos = input_buf + consumed_offset, end = input_buf + input_len.
     * After dispatch, consumed_offset = pos - input_buf.  This lets
     * the state machine resume mid-token without re-scanning. */
    size_t consumed_offset;

    /* Pending attribute name (between ATTR_NAME and ATTR_VALUE).
     * Scratch-owned pointer; valid only while state == ATTR_EQ or
     * ATTR_VALUE. */
    const char* pending_attr_name;

    /* Feature flag: 0 = legacy buffering (default), 1 = streaming
     * state machine. */
    int    streaming;

    /* Emitted start_document once we've committed to parsing --
     * used to avoid emitting twice across feed() calls. */
    int    start_emitted;
    int    end_emitted;
};

#endif /* LEPTRIS_SAX_INTERNAL_H */
