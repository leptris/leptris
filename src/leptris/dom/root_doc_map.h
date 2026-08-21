/* dom/root_doc_map.h — Thread-local root-element → document mapping. */
#ifndef LEPTRIS_DOM_ROOT_DOC_MAP_H
#define LEPTRIS_DOM_ROOT_DOC_MAP_H

#include "element.h"
#include "../memory/pool.h"  /* LeptrisMemoryPool — single canonical typedef */

struct leptris_document;

#ifdef __cplusplus
extern "C" {
#endif

void leptris_root_doc_register(LeptrisElement root, struct leptris_document* doc);
void leptris_root_doc_unregister(LeptrisElement root);
struct leptris_document* leptris_root_doc_lookup(LeptrisElement root);

struct leptris_document* leptris_element_get_document(LeptrisElement elem);
LeptrisMemoryPool* leptris_element_get_pool(LeptrisElement elem);

#ifdef __cplusplus
}
#endif

#endif
