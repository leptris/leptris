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
#ifndef TAURUS_COMMON_CHARTYPE_H
#define TAURUS_COMMON_CHARTYPE_H

#include <stdint.h>

/* Bit flags for character classes. */
#define CT_NAME_START  0x01  /* Letter, '_', ':' — valid XML name start */
#define CT_NAME        0x02  /* CT_NAME_START + '-', '.', '0'-'9' */
#define CT_WS          0x04  /* Space, tab, CR, LF */

/* The shared table. Defined once in chartype.c. */
extern const uint8_t taurus_chartype_table[256];

/* Test a character against one or more flags. Returns 1 or 0. */
#define IS_CHARTYPE(c, flags) \
    (!!(taurus_chartype_table[(unsigned char)(c)] & (flags)))

/* Convenience wrappers mirroring the old per-TU helpers. */
#define IS_NAME_CHAR(c)  IS_CHARTYPE(c, CT_NAME | CT_NAME_START)
#define IS_NAME_START(c) IS_CHARTYPE(c, CT_NAME_START)
#define IS_WS(c)         IS_CHARTYPE(c, CT_WS)

#endif /* TAURUS_COMMON_CHARTYPE_H */
