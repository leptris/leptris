#ifndef LEPTRIS_HTML_H
#define LEPTRIS_HTML_H

/* ============================================================================
 * HTML parsing (issue #659) — the WHATWG-conformant engine plus the
 * libxml2/Nokogiri-compatibility mode. Split out of leptris.h so the
 * HTML surface is discoverable as a subsystem (TODO 99 convention:
 * headers split by subsystem; bindings include sub-headers directly).
 * ============================================================================ */

#include <stddef.h>
#include "types.h"

/* Export macro: mirrors leptris.h/sax.h so html.h is includable
 * standalone. See TODO 80 (visibility preset). */
#ifndef LEPTRIS_API
#  ifdef LEPTRIS_FOR_BINDGEN
#    define LEPTRIS_API
#  elif defined(_WIN32)
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse an HTML string into a document (issue #659)
 *
 * Tolerant HTML4/5 parse into the standard DOM: implied end tags
 * (p/li/td/tr/th/dt/dd/option/...), void elements, raw-text
 * <script>/<style>, minimized + unquoted attribute values,
 * case-insensitive tag/attribute names, and the HTML named-entity
 * table. Behaviors follow libxml2's HTMLparser as exposed by
 * Nokogiri (fragment shape: no synthesized <html>/<head>/<body>,
 * no implied <tbody>). Malformed input never fails the parse — it
 * degrades to text; a document with no nodes at all is an error.
 * The result serializes and queries (XPath/XSLT) like any
 * libleptris document.
 *
 * @param html HTML input (must be valid UTF-8)
 * @param length Length of input in bytes
 * @param status Output status code (can be NULL)
 * @return Document handle or NULL on error
 *
 * Memory: Caller must call leptris_document_free() when done
 * Thread safety: Not thread-safe. One document per thread.
 */
LEPTRIS_API LeptrisDocument leptris_parse_html_string(const char* html,
                                                      size_t length,
                                                      LeptrisStatus* status);

/* HTML4/libxml2-compatibility mode (#659): identical tolerant
 * tokenizer and Nokogiri document shape, except leading
 * script/style content stays in <body> (libxml2's shape —
 * title/meta/link/base still lift into the implied head). Bindings
 * that must match Nokogiri byte-for-byte pin this entry;
 * leptris_parse_html_string is the WHATWG-conformant engine.
 * Memory contract identical to leptris_parse_html_string. */
LEPTRIS_API LeptrisDocument leptris_parse_html4_string(const char* html,
                                                       size_t length,
                                                       LeptrisStatus* status);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_HTML_H */
