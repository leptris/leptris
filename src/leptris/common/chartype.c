/* common/chartype.c — Shared character classification table.
 *
 * The table packs XML name-start, name-char, whitespace, and UTF-8
 * classification into bitflags so callers can test multiple
 * properties in one indexed access (pugixml technique).
 *
 * See chartype.h for the IS_CHARTYPE / IS_NAME_CHAR / IS_WS macros. */

#include "chartype.h"

/* Fully static, const, and complete — no load-time initializer.
 * Issue #626: a previous version OR'd CT_UTF8 into the high half at
 * load time via LEPTRIS_CONSTRUCTOR, and the MSVC .CRT$XCU slot did
 * not run in the leptris-ruby extension, silently rejecting every
 * non-ASCII element name on Windows. Static entries cannot fail to
 * initialize. */
const uint8_t leptris_chartype_table[256] = {
    /* ASCII letters */
    ['a']=CT_NAME_START|CT_NAME, ['b']=CT_NAME_START|CT_NAME,
    ['c']=CT_NAME_START|CT_NAME, ['d']=CT_NAME_START|CT_NAME,
    ['e']=CT_NAME_START|CT_NAME, ['f']=CT_NAME_START|CT_NAME,
    ['g']=CT_NAME_START|CT_NAME, ['h']=CT_NAME_START|CT_NAME,
    ['i']=CT_NAME_START|CT_NAME, ['j']=CT_NAME_START|CT_NAME,
    ['k']=CT_NAME_START|CT_NAME, ['l']=CT_NAME_START|CT_NAME,
    ['m']=CT_NAME_START|CT_NAME, ['n']=CT_NAME_START|CT_NAME,
    ['o']=CT_NAME_START|CT_NAME, ['p']=CT_NAME_START|CT_NAME,
    ['q']=CT_NAME_START|CT_NAME, ['r']=CT_NAME_START|CT_NAME,
    ['s']=CT_NAME_START|CT_NAME, ['t']=CT_NAME_START|CT_NAME,
    ['u']=CT_NAME_START|CT_NAME, ['v']=CT_NAME_START|CT_NAME,
    ['w']=CT_NAME_START|CT_NAME, ['x']=CT_NAME_START|CT_NAME,
    ['y']=CT_NAME_START|CT_NAME, ['z']=CT_NAME_START|CT_NAME,
    ['A']=CT_NAME_START|CT_NAME, ['B']=CT_NAME_START|CT_NAME,
    ['C']=CT_NAME_START|CT_NAME, ['D']=CT_NAME_START|CT_NAME,
    ['E']=CT_NAME_START|CT_NAME, ['F']=CT_NAME_START|CT_NAME,
    ['G']=CT_NAME_START|CT_NAME, ['H']=CT_NAME_START|CT_NAME,
    ['I']=CT_NAME_START|CT_NAME, ['J']=CT_NAME_START|CT_NAME,
    ['K']=CT_NAME_START|CT_NAME, ['L']=CT_NAME_START|CT_NAME,
    ['M']=CT_NAME_START|CT_NAME, ['N']=CT_NAME_START|CT_NAME,
    ['O']=CT_NAME_START|CT_NAME, ['P']=CT_NAME_START|CT_NAME,
    ['Q']=CT_NAME_START|CT_NAME, ['R']=CT_NAME_START|CT_NAME,
    ['S']=CT_NAME_START|CT_NAME, ['T']=CT_NAME_START|CT_NAME,
    ['U']=CT_NAME_START|CT_NAME, ['V']=CT_NAME_START|CT_NAME,
    ['W']=CT_NAME_START|CT_NAME, ['X']=CT_NAME_START|CT_NAME,
    ['Y']=CT_NAME_START|CT_NAME, ['Z']=CT_NAME_START|CT_NAME,
    /* Digits — name chars but not name-start */
    ['0']=CT_NAME, ['1']=CT_NAME, ['2']=CT_NAME, ['3']=CT_NAME,
    ['4']=CT_NAME, ['5']=CT_NAME, ['6']=CT_NAME, ['7']=CT_NAME,
    ['8']=CT_NAME, ['9']=CT_NAME,
    /* Name punctuation */
    ['_']=CT_NAME_START|CT_NAME, [':']=CT_NAME_START|CT_NAME,
    ['-']=CT_NAME, ['.']=CT_NAME,
    /* Whitespace */
    [' ']=CT_WS, ['\t']=CT_WS, ['\n']=CT_WS, ['\r']=CT_WS,
    /* UTF-8 multibyte lead and continuation bytes — every byte
     * >= 0x80 is CT_UTF8 (lenient name scanning, <café>). */
#define CT_U8(n) [n]=CT_UTF8
    CT_U8(0x80), CT_U8(0x81), CT_U8(0x82), CT_U8(0x83),
    CT_U8(0x84), CT_U8(0x85), CT_U8(0x86), CT_U8(0x87),
    CT_U8(0x88), CT_U8(0x89), CT_U8(0x8a), CT_U8(0x8b),
    CT_U8(0x8c), CT_U8(0x8d), CT_U8(0x8e), CT_U8(0x8f),
    CT_U8(0x90), CT_U8(0x91), CT_U8(0x92), CT_U8(0x93),
    CT_U8(0x94), CT_U8(0x95), CT_U8(0x96), CT_U8(0x97),
    CT_U8(0x98), CT_U8(0x99), CT_U8(0x9a), CT_U8(0x9b),
    CT_U8(0x9c), CT_U8(0x9d), CT_U8(0x9e), CT_U8(0x9f),
    CT_U8(0xa0), CT_U8(0xa1), CT_U8(0xa2), CT_U8(0xa3),
    CT_U8(0xa4), CT_U8(0xa5), CT_U8(0xa6), CT_U8(0xa7),
    CT_U8(0xa8), CT_U8(0xa9), CT_U8(0xaa), CT_U8(0xab),
    CT_U8(0xac), CT_U8(0xad), CT_U8(0xae), CT_U8(0xaf),
    CT_U8(0xb0), CT_U8(0xb1), CT_U8(0xb2), CT_U8(0xb3),
    CT_U8(0xb4), CT_U8(0xb5), CT_U8(0xb6), CT_U8(0xb7),
    CT_U8(0xb8), CT_U8(0xb9), CT_U8(0xba), CT_U8(0xbb),
    CT_U8(0xbc), CT_U8(0xbd), CT_U8(0xbe), CT_U8(0xbf),
    CT_U8(0xc0), CT_U8(0xc1), CT_U8(0xc2), CT_U8(0xc3),
    CT_U8(0xc4), CT_U8(0xc5), CT_U8(0xc6), CT_U8(0xc7),
    CT_U8(0xc8), CT_U8(0xc9), CT_U8(0xca), CT_U8(0xcb),
    CT_U8(0xcc), CT_U8(0xcd), CT_U8(0xce), CT_U8(0xcf),
    CT_U8(0xd0), CT_U8(0xd1), CT_U8(0xd2), CT_U8(0xd3),
    CT_U8(0xd4), CT_U8(0xd5), CT_U8(0xd6), CT_U8(0xd7),
    CT_U8(0xd8), CT_U8(0xd9), CT_U8(0xda), CT_U8(0xdb),
    CT_U8(0xdc), CT_U8(0xdd), CT_U8(0xde), CT_U8(0xdf),
    CT_U8(0xe0), CT_U8(0xe1), CT_U8(0xe2), CT_U8(0xe3),
    CT_U8(0xe4), CT_U8(0xe5), CT_U8(0xe6), CT_U8(0xe7),
    CT_U8(0xe8), CT_U8(0xe9), CT_U8(0xea), CT_U8(0xeb),
    CT_U8(0xec), CT_U8(0xed), CT_U8(0xee), CT_U8(0xef),
    CT_U8(0xf0), CT_U8(0xf1), CT_U8(0xf2), CT_U8(0xf3),
    CT_U8(0xf4), CT_U8(0xf5), CT_U8(0xf6), CT_U8(0xf7),
    CT_U8(0xf8), CT_U8(0xf9), CT_U8(0xfa), CT_U8(0xfb),
    CT_U8(0xfc), CT_U8(0xfd), CT_U8(0xfe), CT_U8(0xff),
#undef CT_U8
};
