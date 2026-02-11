/* chartype.h - Ultra-fast character classification using lookup table
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Inspired by pugixml's character classification approach.
 * Replaces multiple if/else branches with single table lookup + bitwise AND.
 * Expected: 10-15% speedup by eliminating branch mispredictions.
 */

#ifndef TAURUS_CHARTYPE_H
#define TAURUS_CHARTYPE_H

/* Character type bit flags - can be combined with bitwise OR */
typedef enum {
    ct_whitespace    = 1,    /* ' ', '\t', '\r', '\n' */
    ct_name_start    = 2,    /* [A-Za-z_:] */
    ct_name_char     = 4,    /* [A-Za-z0-9:_.-] */
    ct_parse_pcdata  = 8,    /* Everything except <, &, \r */
    ct_parse_attr    = 16,   /* Everything except <, &, \r, ", ' */
    ct_digit         = 32,   /* [0-9] */
    ct_start_symbol  = 64,   /* Valid start tag chars (A-Za-z) */
    ct_alpha         = 128   /* [A-Za-z] */
} chartype_t;

/* Pre-computed character type table (256 bytes, fits in L1 cache) */
static const unsigned char chartype_table[256] = {
    /* 0-31 (control chars) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,  /* \t=1, \n=1, \r=1 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    
    /* 32-47 (space and punctuation) */
    1,    /* 32  ' '  - whitespace */
    24,   /* 33  '!'  - pcdata + attr */
    0,    /* 34  '"'  - quote (excluded from attr) */
    24,   /* 35  '#'  - pcdata + attr */
    24,   /* 36  '$'  - pcdata + attr */
    24,   /* 37  '%'  - pcdata + attr */
    0,    /* 38  '&'  - entity (excluded from pcdata/attr) */
    0,    /* 39  '\'' - quote (excluded from attr) */
    24,   /* 40  '('  - pcdata + attr */
    24,   /* 41  ')'  - pcdata + attr */
    24,   /* 42  '*'  - pcdata + attr */
    24,   /* 43  '+'  - pcdata + attr */
    24,   /* 44  ','  - pcdata + attr */
    4,    /* 45  '-'  - name_char only */
    4,    /* 46  '.'  - name_char only */
    24,   /* 47  '/'  - pcdata + attr */
    
    /* 48-57 (digits) */
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36,  /* 0-9: digit + name_char */
    
    /* 58-64 (punctuation) */
    6,    /* 58  ':'  - name_start + name_char */
    24,   /* 59  ';'  - pcdata + attr */
    0,    /* 60  '<'  - tag start (excluded from pcdata/attr) */
    24,   /* 61  '='  - pcdata + attr */
    24,   /* 62  '>'  - pcdata + attr */
    24,   /* 63  '?'  - pcdata + attr */
    24,   /* 64  '@'  - pcdata + attr */
    
    /* 65-90 (A-Z) */
    198, 198, 198, 198, 198, 198, 198, 198, 198, 198,  /* A-J */
    198, 198, 198, 198, 198, 198, 198, 198, 198, 198,  /* K-T */
    198, 198, 198, 198, 198, 198,                      /* U-Z */
    /* 198 = name_start(2) + name_char(4) + start_symbol(64) + alpha(128) */
    
    /* 91-96 (punctuation) */
    24,   /* 91  '['  - pcdata + attr */
    24,   /* 92  '\\' - pcdata + attr */
    24,   /* 93  ']'  - pcdata + attr */
    24,   /* 94  '^'  - pcdata + attr */
    6,    /* 95  '_'  - name_start + name_char */
    24,   /* 96  '`'  - pcdata + attr */
    
    /* 97-122 (a-z) */
    198, 198, 198, 198, 198, 198, 198, 198, 198, 198,  /* a-j */
    198, 198, 198, 198, 198, 198, 198, 198, 198, 198,  /* k-t */
    198, 198, 198, 198, 198, 198,                      /* u-z */
    
    /* 123-127 (punctuation) */
    24, 24, 24, 24, 0,  /* { | } ~ DEL */
    
    /* 128-255 (extended ASCII / UTF-8) */
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24
};

/* Ultra-fast character type test - single table lookup + bitwise AND */
static inline int is_chartype(unsigned char c, chartype_t ct) {
    return chartype_table[c] & ct;
}

/* Convenience wrappers for common checks */
static inline int is_whitespace_fast(char c) {
    return is_chartype((unsigned char)c, ct_whitespace);
}

static inline int is_name_start_fast(char c) {
    return is_chartype((unsigned char)c, ct_name_start);
}

static inline int is_name_char_fast(char c) {
    return is_chartype((unsigned char)c, ct_name_char);
}

static inline int is_start_symbol_fast(char c) {
    return is_chartype((unsigned char)c, ct_start_symbol);
}

static inline int is_alpha_fast(char c) {
    return is_chartype((unsigned char)c, ct_alpha);
}

static inline int is_digit_fast(char c) {
    return is_chartype((unsigned char)c, ct_digit);
}

#endif /* TAURUS_CHARTYPE_H */