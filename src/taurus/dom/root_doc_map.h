/* dom/root_doc_map.h — Thread-local root-element → document mapping. */
#ifndef TAURUS_DOM_ROOT_DOC_MAP_H
#define TAURUS_DOM_ROOT_DOC_MAP_H

#include "element.h"

struct taurus_document;
typedef struct taurus_memory_pool TaurusMemoryPool;

#ifdef __cplusplus
extern "C" {
#endif

void taurus_root_doc_register(TaurusElement root, struct taurus_document* doc);
void taurus_root_doc_unregister(TaurusElement root);
struct taurus_document* taurus_root_doc_lookup(TaurusElement root);

struct taurus_document* taurus_element_get_document(TaurusElement elem);
TaurusMemoryPool* taurus_element_get_pool(TaurusElement elem);

#ifdef __cplusplus
}
#endif

#endif
