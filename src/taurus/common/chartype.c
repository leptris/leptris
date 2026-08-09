/* common/chartype.c — Shared character classification table.
 *
 * The table packs XML name-start, name-char, and whitespace
 * classification into bitflags so callers can test multiple
 * properties in one indexed access (pugixml technique).
 *
 * See chartype.h for the IS_CHARTYPE / IS_NAME_CHAR / IS_WS macros. */

#include "chartype.h"

const uint8_t taurus_chartype_table[256] = {
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
};
