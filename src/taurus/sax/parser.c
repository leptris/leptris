/**
 * @file sax/parser.c
 * @brief SAX parser — public API surface
 *
 * TODO 116 Phase C: the recursive-descent parser that used to live
 * here is gone.  All parsing — one-shot (`taurus_sax_parse`) and
 * incremental (`taurus_sax_parser_feed`) — goes through the explicit
 * state machine in streaming.c.
 *
 * This file is now: struct layout (in sax_internal.h, shared with
 * streaming.c), parser lifecycle (create / free), feed dispatch, and
 * the one-shot convenience wrapper.  All parsing logic is in
 * streaming.c.
 */

#include "../../include/taurus/sax/sax.h"
#include "sax_internal.h"
#include "../taurus_internal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* Streaming state machine — implemented in streaming.c. */
int  taurus_sax_streaming_feed(TaurusSAXParser* p,
                               const char* xml, size_t len, int is_final);
void taurus_sax_streaming_reset(TaurusSAXParser* p);

/* ============================================================================
 * One-shot parse — convenience wrapper around the streaming state machine.
 * ============================================================================ */

int taurus_sax_parse(const char* xml, size_t len,
                     TaurusSAXHandler* handler,
                     void* user_data) {
    if (!xml || len == 0 || !handler) return -1;

    TaurusSAXParser* p = taurus_sax_parser_create(handler, user_data);
    if (!p) return -1;

    int rc = taurus_sax_parser_feed(p, xml, len, 1);
    int has_error = p->has_error;
    taurus_sax_parser_free(p);

    return (rc == 0 && !has_error) ? 0 : -1;
}

/* ============================================================================
 * Parser lifecycle
 * ============================================================================ */

TaurusSAXParser* taurus_sax_parser_create(TaurusSAXHandler* handler, void* user_data) {
    if (!handler) return NULL;

    TaurusSAXParser* parser = (TaurusSAXParser*)malloc(sizeof(TaurusSAXParser));
    if (!parser) return NULL;

    parser->handler = handler;
    parser->user_data = user_data;
    parser->pos = NULL;
    parser->end = NULL;
    parser->line = 1;
    parser->column = 1;
    parser->has_error = 0;
    parser->error_message[0] = '\0';
    parser->scratch = NULL;
    parser->scratch_len = 0;
    parser->scratch_cap = 0;
    parser->input_buf = NULL;
    parser->input_len = 0;
    parser->input_cap = 0;
    parser->state = SAX_ST_TOPLEVEL;
    parser->elem_depth = 0;
    parser->carry = NULL;
    parser->carry_len = 0;
    parser->carry_cap = 0;
    parser->consumed_offset = 0;
    parser->pending_attr_name = NULL;
    parser->streaming = 1;  /* TODO 116 Phase B: streaming is the default. */
    parser->start_emitted = 0;
    parser->end_emitted = 0;

    return parser;
}

int taurus_sax_parser_feed(TaurusSAXParser* parser,
                            const char* xml,
                            size_t len,
                            int is_final) {
    if (!parser || !xml) return -1;
    return taurus_sax_streaming_feed(parser, xml, len, is_final);
}

void taurus_sax_parser_free(TaurusSAXParser* parser) {
    if (parser) {
        taurus_sax_streaming_reset(parser);
        free(parser->input_buf);
        free(parser->scratch);
        free(parser);
    }
}
