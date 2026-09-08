/* rng/rng_internal.h — RELAX NG pattern IR (#878 phase 1).
 *
 * The XML-syntax schema (parsed by the standard XML parser) lowers
 * into this pattern tree. Every RELAX NG pattern has one node kind;
 * children link via first_child/next. define/ref carry the grammar
 * layer; combine attributes resolve at parse time.
 */
#ifndef LEPTRIS_RNG_INTERNAL_H
#define LEPTRIS_RNG_INTERNAL_H

#include "../include/leptris.h"
#include <stddef.h>

typedef enum {
    RNG_EMPTY = 0,
    RNG_NOT_ALLOWED,
    RNG_TEXT,
    RNG_ELEMENT,
    RNG_ATTRIBUTE,
    RNG_CHOICE,
    RNG_INTERLEAVE,
    RNG_GROUP,
    RNG_OPTIONAL,
    RNG_ZERO_OR_MORE,
    RNG_ONE_OR_MORE,
    RNG_LIST,
    RNG_MIXED,
    RNG_DATA,
    RNG_VALUE,
    RNG_PARAM,
    RNG_EXCEPT,
    RNG_GRAMMAR,
    RNG_DEFINE,
    RNG_REF,
} RngPatternKind;

typedef struct RngPattern RngPattern;
typedef struct RngDefine RngDefine;
typedef struct RngGrammar RngGrammar;

struct RngPattern {
    RngPatternKind kind;
    RngPattern* first_child;
    RngPattern* next;
    char* name;          /* ELEMENT/ATTRIBUTE/VALUE/@name, DEFINE name */
    char* ns;            /* ELEMENT/ATTRIBUTE namespace URI */
    char* datatype;      /* DATA type local name, VALUE datatype */
    char* datatype_lib;  /* DATA/VALUE datatypeLibrary */
    char* value;         /* VALUE text, PARAM value */
    RngDefine* define;   /* resolved target for REF (phase 2 link) */
};

/* A named <define>. combine="choice|interleave" merges bodies at
 * parse time. */
struct RngDefine {
    char* name;
    char* combine;       /* NULL, "choice", "interleave" */
    RngPattern* body;
    RngDefine* next;
    int refs;            /* ref sites waiting for phase-2 linking */
};

struct RngGrammar {
    RngDefine* defines;
    RngPattern* start;   /* resolved <start> body (combined) */
    char* default_ns;
    char* default_lib;
};

/* The public opaque handle. */
struct leptris_relaxng {
    RngGrammar* grammar;
    char* error;         /* first parse error (Jing-shaped later) */
};

/* parse.c */
struct leptris_document;

/* validate.c — returns 1 valid, 0 invalid (rng->error carries the
 * Jing-shaped first failure). Phase-2 subset. */
int rng_validate_document(struct leptris_relaxng* rng,
                          struct leptris_document* doc);
struct leptris_relaxng* rng_parse_document(LeptrisDocument doc);
struct leptris_relaxng* rng_parse_file(const char* path);

/* free.c duties are inline in the public entry */
void rng_pattern_free(RngPattern* p);
void rng_grammar_free(RngGrammar* g);

#endif
