/* flat/flat_xpath.h — FlatDoc-direct XPath dispatch (TODO 145 Phase 3).
 *
 * Pattern-matching dispatcher for primitive-returning XPath queries.
 * When the doc has a parsed FlatDoc and hasn't been promoted, certain
 * simple query patterns evaluate against FlatDoc directly, skipping
 * the compact-pointer promote pass entirely.
 *
 * Supported patterns (case-sensitive function names):
 *   - count(//name)            → flat count by element name
 *   - count(//*)               → flat count all elements
 *   - boolean(//name)          → flat exists check
 *   - count(descendant::name)  → same as count(//name)
 *   - count(descendant-or-self::name)
 *
 * Returns 1 (handled) with the result filled in via *out_result,
 * or 0 (not handled) to let the caller fall back to the normal
 * compact-tree XPath evaluation.
 */
#ifndef TAURUS_FLAT_FLAT_XPATH_H
#define TAURUS_FLAT_FLAT_XPATH_H

#include "flat_doc.h"
#include "../taurus_internal.h"

/* Try to evaluate `expression` against the doc's FlatDoc.
 *
 * Pre: doc must have flat_doc set and not be promoted.
 * Post: if return is 1, *out_result is a freshly-allocated
 *       TaurusXPathResult that the caller frees via
 *       taurus_xpath_result_free.
 *
 * Returns: 1 if handled (caller uses *out_result)
 *          0 if not handled (caller falls back to normal path)
 */
int flat_xpath_try_eval(struct taurus_document* doc,
                         const char* expression,
                         struct taurus_xpath_result** out_result);

#endif /* TAURUS_FLAT_FLAT_XPATH_H */
