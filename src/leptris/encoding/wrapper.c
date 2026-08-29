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

#include "encoding.h"   /* parse_declaration (declaration parsing is
                          * iconv-free; auto_convert stays iconv-only) */

/* Local alias for the macro defined in leptris_internal.h.  Kept local
 * so this file can be moved without dragging the macro definition. */
#define WRAPPER_FREE(ptr) do { if (ptr) { free(ptr); (ptr) = NULL; } } while (0)

char* leptris_encoding_parse_declaration(const char* xml, size_t len) {
    if (!xml || len < 20) {  /* Minimum: <?xml encoding=""?> */
        return NULL;
    }

    /* Check for <?xml prefix */
    if (strncmp(xml, "<?xml", 5) != 0) {
        return NULL;
    }

    /* Find encoding=" */
    const char* encoding_start = strstr(xml, "encoding");
    if (!encoding_start || encoding_start - xml > (long)len) {
        return NULL;
    }

    /* Skip "encoding" and whitespace */
    encoding_start += 8;
    while (encoding_start - xml < (long)len && (*encoding_start == ' ' || *encoding_start == '\t')) {
        encoding_start++;
    }

    /* Expect '=' */
    if (encoding_start - xml >= (long)len || *encoding_start != '=') {
        return NULL;
    }
    encoding_start++;

    /* Skip whitespace after '=' */
    while (encoding_start - xml < (long)len && (*encoding_start == ' ' || *encoding_start == '\t')) {
        encoding_start++;
    }

    /* Expect quote (single or double) */
    if (encoding_start - xml >= (long)len) {
        return NULL;
    }
    char quote = *encoding_start;
    if (quote != '"' && quote != '\'') {
        return NULL;
    }
    encoding_start++;

    /* Find closing quote */
    const char* encoding_end = encoding_start;
    while (encoding_end - xml < (long)len && *encoding_end != quote) {
        encoding_end++;
    }

    if (encoding_end - xml >= (long)len) {
        return NULL;
    }

    /* Extract encoding name */
    size_t encoding_len = encoding_end - encoding_start;
    if (encoding_len == 0 || encoding_len > 64) {
        return NULL;
    }

    char* encoding_name = malloc(encoding_len + 1);
    if (!encoding_name) {
        return NULL;
    }

    memcpy(encoding_name, encoding_start, encoding_len);
    encoding_name[encoding_len] = '\0';

    return encoding_name;
}

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
    /* No iconv. Issue #613: a declared Latin-1 input still owes the
     * caller UTF-8 content — every binding decodes text as UTF-8, so
     * silently passing the raw latin-1 bytes through trades a parse
     * error for a UnicodeDecodeError on first .text. Latin-1 is
     * computable without iconv (each byte maps to U+00xx); other
     * single-byte encodings keep the assume-UTF-8 fallback, where
     * invalid sequences surface as parse errors. */
    {
        char* declared = leptris_encoding_parse_declaration(xml, length);
        int latin1 = 0;
        if (declared) {
            if (strcmp(declared, "ISO-8859-1") == 0 ||
                strcmp(declared, "iso-8859-1") == 0 ||
                strcmp(declared, "ISO8859-1") == 0 ||
                strcmp(declared, "LATIN1") == 0 ||
                strcmp(declared, "latin1") == 0 ||
                strcmp(declared, "LATIN-1") == 0 ||
                strcmp(declared, "latin-1") == 0)
                latin1 = 1;
        }
        if (latin1) {
            size_t cap = length * 2 + 1;
            char* utf8 = (char*)malloc(cap);
            if (!utf8) {
                free(declared);
                if (status) *status = LEPTRIS_ERROR_MEMORY;
                return NULL;
            }
            size_t w = 0;
            for (size_t i = 0; i < length; i++) {
                unsigned char c = (unsigned char)xml[i];
                if (c < 0x80) {
                    utf8[w++] = (char)c;
                } else {
                    utf8[w++] = (char)(0xC0 | (c >> 6));
                    utf8[w++] = (char)(0x80 | (c & 0x3F));
                }
            }
            utf8[w] = '\0';
            struct leptris_document* doc = leptris_parse(utf8, w);
            free(utf8);
            if (doc) {
                if (doc->encoding) WRAPPER_FREE(doc->encoding);
                doc->encoding = leptris_strdup("ISO-8859-1");
            } else if (status) {
                *status = LEPTRIS_ERROR_PARSE;
            }
            free(declared);
            return doc;
        }
        free(declared);
    }
    /* No iconv — assume input is UTF-8 and let the parser validate. */
    return leptris_parse_string(xml, length, status);
#endif
}
