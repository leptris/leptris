/* common/chartype.h — Shared character classification table.
 *
 * Single 256-byte lookup table with bitflags, shared between
 * direct_parse and flat_parser. Modeled on pugixml's
 * g_chartype_table technique: one indexed access per byte, no
 * branch chains, branch-predictor friendly.
 *
 * Previously direct_parse and flat_parser each had their own set
 * of 256-byte boolean tables (dp_name_char_lut, dp_name_start_lut,
 * dp_ws_lut, fp_*). This consolidation is DRY and cuts the binary
 * size by 3 × 256 bytes per TU. */
#ifndef LEPTRIS_COMMON_CHARTYPE_H
#define LEPTRIS_COMMON_CHARTYPE_H

#include <stdint.h>

/* Bit flags for character classes. */
#define CT_NAME_START  0x01  /* Letter, '_', ':' — valid XML name start */
#define CT_NAME        0x02  /* CT_NAME_START + '-', '.', '0'-'9' */
#define CT_WS          0x04  /* Space, tab, CR, LF */
#define CT_UTF8        0x08  /* Bytes >= 0x80: UTF-8 multibyte sequences.
                              * Included in name-char/name-start tests so
                              * Unicode names (<café>) aren't truncated.
                              * Matches flat_parser's lenient UTF-8
                              * fallback (c >= 0xC0 for start, c >= 0x80
                              * for continuation). */

/* The shared table. Defined once in chartype.c — fully static and
 * const, including the CT_UTF8 entries for bytes >= 0x80 (no
 * load-time initializer; issue #626). */
extern const uint8_t leptris_chartype_table[256];

/* Test a character against one or more flags. Returns 1 or 0. */
#define IS_CHARTYPE(c, flags) \
    (!!(leptris_chartype_table[(unsigned char)(c)] & (flags)))

/* Convenience wrappers mirroring the old per-TU helpers.
 * CT_UTF8 is folded into name-char/name-start so that UTF-8
 * multibyte names scan without truncation. */
#define IS_NAME_CHAR(c)  IS_CHARTYPE(c, CT_NAME | CT_NAME_START | CT_UTF8)
#define IS_NAME_START(c) IS_CHARTYPE(c, CT_NAME_START | CT_UTF8)
#define IS_WS(c)         IS_CHARTYPE(c, CT_WS)

#endif /* LEPTRIS_COMMON_CHARTYPE_H */
