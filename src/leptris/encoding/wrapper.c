/* encoding/wrapper.c — encoding-aware parse wrappers.
 *
 * Extracted from leptris.c as phase 1 of the file-split roadmap
 * (TODO 42/54/73).  Self-contained: depends only on the parser entry
 * point, the UTF-16 detector, and (optionally) iconv.  No private
 * state from leptris.c.
 *
 * Memory ownership (see TODO 25 / 33):
 *   - Intermediate UTF-16 / iconv conversion buffers are calloc'd
 *     here and freed here — `leptris_parse` makes its own pool copy.
 *   - The `detected_encoding` string returned by iconv is malloc'd
 *     by the iconv glue and freed here after copying into the doc.
 */

#include "../../include/leptris.h"
#include "../leptris_internal.h"
#include "utf16.h"

#include <stdlib.h>
#include <string.h>

#ifdef LEPTRIS_HAS_ICONV
#include "encoding.h"
#endif

/* Local alias for the macro defined in leptris_internal.h.  Kept local
 * so this file can be moved without dragging the macro definition. */
#define WRAPPER_FREE(ptr) do { if (ptr) { free(ptr); (ptr) = NULL; } } while (0)

LEPTRIS_API LeptrisDocument leptris_parse_string_with_encoding(
    const char* xml, size_t length, LeptrisStatus* status) {

    if (status) *status = LEPTRIS_OK;

    const unsigned char* data = (const unsigned char*)xml;
    utf16_bom_t bom = utf16_detect_bom(data, length);

    /* ---- UTF-16 with BOM ---- */
    if (bom == UTF16_BOM_LE || bom == UTF16_BOM_BE) {
        utf16_encoding_t enc = (bom == UTF16_BOM_LE) ? UTF16_LE : UTF16_BE;

        size_t utf8_size = utf16_to_utf8_size(data, length, enc);
        char* utf8_buffer = (char*)malloc(utf8_size);
        if (!utf8_buffer) {
            if (status) *status = LEPTRIS_ERROR_MEMORY;
            return NULL;
        }

        size_t utf8_len = utf16_to_utf8(data, length, utf8_buffer, utf8_size, enc);
        utf8_buffer[utf8_len] = '\0';

        struct leptris_document* doc = leptris_parse(utf8_buffer, utf8_len);

        /* leptris_parse() heap-copies its input into doc->xml_buffer;
         * our intermediate is now redundant. */
        WRAPPER_FREE(utf8_buffer);

        if (doc) {
            if (doc->encoding) WRAPPER_FREE(doc->encoding);
            doc->encoding = leptris_strdup((enc == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        }

        if (!doc && status) *status = LEPTRIS_ERROR_PARSE;
        return doc;
    }

    /* ---- UTF-16 without BOM (heuristic) ---- */
    utf16_encoding_t detected = utf16_detect_encoding(data, length);
    if (detected == UTF16_LE || detected == UTF16_BE) {
        size_t utf8_size = utf16_to_utf8_size(data, length, detected);
        char* utf8_buffer = (char*)malloc(utf8_size);
        if (!utf8_buffer) {
            if (status) *status = LEPTRIS_ERROR_MEMORY;
            return NULL;
        }

        size_t utf8_len = utf16_to_utf8(data, length, utf8_buffer, utf8_size, detected);
        utf8_buffer[utf8_len] = '\0';

        struct leptris_document* doc = leptris_parse(utf8_buffer, utf8_len);
        WRAPPER_FREE(utf8_buffer);

        if (doc) {
            if (doc->encoding) WRAPPER_FREE(doc->encoding);
            doc->encoding = leptris_strdup((detected == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        }

        if (!doc && status) *status = LEPTRIS_ERROR_PARSE;
        return doc;
    }

#ifdef LEPTRIS_HAS_ICONV
    /* ---- iconv path: ISO-8859, Shift-JIS, EBCDIC, etc. ---- */
    size_t utf8_len = 0;
    char* detected_encoding = NULL;
    char* utf8_xml = leptris_encoding_auto_convert(
        xml, length, &utf8_len, &detected_encoding);

    if (!utf8_xml) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        if (detected_encoding) free(detected_encoding);
        return NULL;
    }

    struct leptris_document* doc = leptris_parse(utf8_xml, utf8_len);

    if (doc && detected_encoding) {
        if (doc->encoding) WRAPPER_FREE(doc->encoding);
        doc->encoding = leptris_strdup(detected_encoding);
    }

    /* leptris_parse already copied utf8_xml into doc->xml_buffer. */
    if (utf8_xml != xml) free(utf8_xml);
    if (detected_encoding) free(detected_encoding);

    if (!doc && status) *status = LEPTRIS_ERROR_PARSE;
    return doc;
#else
    /* No iconv — assume input is UTF-8 and let the parser validate. */
    return leptris_parse_string(xml, length, status);
#endif
}
