/* format_number.h — JDK1.1 decimal-format pattern core (SSOT).
 *
 * Shared by the XSLT xsl:decimal-format machinery (which resolves
 * a named format to separator strings) and the plain-XPath
 * fn:format-number (default format only). The pattern grammar and
 * digit formatting live here exactly once. */
#ifndef LEPTRIS_FORMAT_NUMBER_H
#define LEPTRIS_FORMAT_NUMBER_H

#include <stddef.h>

typedef struct {
    const char* decimal_sep;
    const char* grouping_sep;
    const char* nan;
    const char* infinity;
    char zero_digit;
    char minus_sign;
} LeptrisNumFormatSpec;

/* Returns a malloc'd lexical form; never NULL (errors fall back to
 * the pattern "0"). */
char* leptris_format_number_core(double value, const char* pattern,
                                 const LeptrisNumFormatSpec* spec);

/* The XPath default decimal format. */
const LeptrisNumFormatSpec* leptris_format_number_default(void);

#endif
