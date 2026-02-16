/* observer.c - Document change observer implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements the observer pattern for document change tracking.
 * This enables undo/redo systems, audit logging, and reactive updates.
 */

#include "../include/taurus/observer.h"
#include "../include/taurus/types.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * Observer entry - stores callback and metadata
 */
typedef struct {
    int id;                      /* Unique observer ID */
    TaurusObserverCallback callback;
    void* userdata;
    int flags;                   /* Event type filter */
    int active;                  /* 1 if observer is active */
} ObserverEntry;

/**
 * Observer list - stored per-document
 */
typedef struct {
    ObserverEntry* entries;      /* Array of observers */
    size_t count;                /* Number of observers */
    size_t capacity;             /* Array capacity */
    int next_id;                 /* Next observer ID to assign */
    int suspended;               /* Non-zero if notifications suspended */
} ObserverList;

/* Global ID counter for unique observer IDs */
static int g_next_observer_id = 1;

/* ============================================================================
 * Observer List Management (Internal)
 * ============================================================================ */

/**
 * Create a new observer list.
 */
ObserverList* observer_list_new(void) {
    ObserverList* list = (ObserverList*)malloc(sizeof(ObserverList));
    if (!list) return NULL;

    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->next_id = g_next_observer_id++;
    list->suspended = 0;

    return list;
}

/**
 * Free an observer list.
 */
void observer_list_free(ObserverList* list) {
    if (!list) return;

    if (list->entries) {
        free(list->entries);
    }
    free(list);
}

/**
 * Add an observer to the list.
 */
static int observer_list_add(ObserverList* list, TaurusObserverCallback callback,
                             void* userdata, int flags) {
    if (!list || !callback) return -1;

    /* Grow array if needed */
    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        ObserverEntry* new_entries = (ObserverEntry*)realloc(
            list->entries, new_capacity * sizeof(ObserverEntry));
        if (!new_entries) return -1;

        list->entries = new_entries;
        list->capacity = new_capacity;
    }

    /* Add entry */
    int id = g_next_observer_id++;
    ObserverEntry* entry = &list->entries[list->count++];
    entry->id = id;
    entry->callback = callback;
    entry->userdata = userdata;
    entry->flags = flags;
    entry->active = 1;

    return id;
}

/**
 * Remove an observer by ID.
 */
static int observer_list_remove(ObserverList* list, int id) {
    if (!list) return TAURUS_ERROR_NULL_ARG;

    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].id == id && list->entries[i].active) {
            list->entries[i].active = 0;
            return TAURUS_OK;
        }
    }

    return TAURUS_ERROR_NOT_FOUND;
}

/**
 * Count active observers.
 */
static size_t observer_list_count(ObserverList* list) {
    if (!list) return 0;

    size_t count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].active) count++;
    }
    return count;
}

/* ============================================================================
 * Event Emission (Internal API)
 * ============================================================================ */

/**
 * Check if event type matches observer flags.
 */
static int event_matches_flags(TaurusEventType type, int flags) {
    if (flags == TAURUS_OBSERVE_ALL) return 1;

    switch (type) {
        case TAURUS_EVENT_ELEMENT_CREATED:
        case TAURUS_EVENT_ELEMENT_ADDED:
        case TAURUS_EVENT_ELEMENT_REMOVED:
        case TAURUS_EVENT_ELEMENT_DESTROYED:
            return (flags & TAURUS_OBSERVE_ELEMENTS) != 0;

        case TAURUS_EVENT_ATTRIBUTE_SET:
        case TAURUS_EVENT_ATTRIBUTE_REMOVED:
            return (flags & TAURUS_OBSERVE_ATTRIBUTES) != 0;

        case TAURUS_EVENT_TEXT_CHANGED:
        case TAURUS_EVENT_NAME_CHANGED:
            return (flags & TAURUS_OBSERVE_CONTENT) != 0;

        case TAURUS_EVENT_NAMESPACE_ADDED:
        case TAURUS_EVENT_NAMESPACE_REMOVED:
            return (flags & TAURUS_OBSERVE_NAMESPACES) != 0;

        case TAURUS_EVENT_DOCUMENT_CLEARED:
        case TAURUS_EVENT_DOCUMENT_FREEING:
            return (flags & TAURUS_OBSERVE_DOCUMENT) != 0;

        default:
            return 1;  /* Unknown events pass through */
    }
}

/**
 * Emit an event to all observers (internal function).
 *
 * This is called by DOM modification functions.
 */
void taurus_emit_event(struct taurus_document* doc, TaurusEventType type,
                       struct taurus_element* target, struct taurus_element* parent,
                       struct taurus_element* sibling, const char* name,
                       const char* old_value, const char* new_value) {
    if (!doc) return;

    ObserverList* list = (ObserverList*)doc->observer_list;
    if (!list) return;
    if (list->suspended) return;
    if (list->count == 0) return;

    /* Build event structure */
    TaurusEvent event = {
        .type = type,
        .doc = doc,
        .target = target,
        .parent = parent,
        .sibling = sibling,
        .name = name,
        .old_value = old_value,
        .new_value = new_value,
        .userdata = NULL,
        .reserved = {NULL, NULL, NULL, NULL}
    };

    /* Notify all active observers */
    for (size_t i = 0; i < list->count; i++) {
        ObserverEntry* entry = &list->entries[i];
        if (!entry->active) continue;

        /* Check event filter */
        if (!event_matches_flags(type, entry->flags)) continue;

        /* Set userdata for this callback */
        event.userdata = entry->userdata;

        /* Call observer */
        entry->callback(&event, entry->userdata);
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

TAURUS_API int taurus_document_add_observer(
    struct taurus_document* doc,
    TaurusObserverCallback callback,
    void* userdata
) {
    return taurus_document_add_observer_filtered(doc, callback, userdata, TAURUS_OBSERVE_ALL);
}

TAURUS_API int taurus_document_add_observer_filtered(
    struct taurus_document* doc,
    TaurusObserverCallback callback,
    void* userdata,
    int flags
) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    if (!callback) return TAURUS_ERROR_NULL_ARG;

    /* Create observer list if needed */
    if (!doc->observer_list) {
        doc->observer_list = observer_list_new();
        if (!doc->observer_list) return TAURUS_ERROR_MEMORY;
    }

    ObserverList* list = (ObserverList*)doc->observer_list;
    return observer_list_add(list, callback, userdata, flags);
}

TAURUS_API int taurus_document_remove_observer(
    struct taurus_document* doc,
    int observer_id
) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    if (!doc->observer_list) return TAURUS_ERROR_NOT_FOUND;

    ObserverList* list = (ObserverList*)doc->observer_list;
    return observer_list_remove(list, observer_id);
}

TAURUS_API int taurus_document_clear_observers(struct taurus_document* doc) {
    if (!doc) return 0;

    ObserverList* list = (ObserverList*)doc->observer_list;
    if (!list) return 0;

    size_t count = observer_list_count(list);
    observer_list_free(list);
    doc->observer_list = NULL;

    return (int)count;
}

TAURUS_API int taurus_document_has_observers(struct taurus_document* doc) {
    if (!doc || !doc->observer_list) return 0;

    ObserverList* list = (ObserverList*)doc->observer_list;
    return observer_list_count(list) > 0 ? 1 : 0;
}

TAURUS_API int taurus_document_observer_count(struct taurus_document* doc) {
    if (!doc || !doc->observer_list) return 0;

    ObserverList* list = (ObserverList*)doc->observer_list;
    return (int)observer_list_count(list);
}

TAURUS_API void taurus_document_suspend_observers(struct taurus_document* doc) {
    if (!doc || !doc->observer_list) return;

    ObserverList* list = (ObserverList*)doc->observer_list;
    list->suspended = 1;
}

TAURUS_API void taurus_document_resume_observers(struct taurus_document* doc) {
    if (!doc || !doc->observer_list) return;

    ObserverList* list = (ObserverList*)doc->observer_list;
    list->suspended = 0;
}

TAURUS_API int taurus_document_observers_suspended(struct taurus_document* doc) {
    if (!doc || !doc->observer_list) return 0;

    ObserverList* list = (ObserverList*)doc->observer_list;
    return list->suspended;
}

TAURUS_API const char* taurus_event_type_name(TaurusEventType type) {
    switch (type) {
        case TAURUS_EVENT_ELEMENT_CREATED:    return "ELEMENT_CREATED";
        case TAURUS_EVENT_ELEMENT_ADDED:      return "ELEMENT_ADDED";
        case TAURUS_EVENT_ELEMENT_REMOVED:    return "ELEMENT_REMOVED";
        case TAURUS_EVENT_ELEMENT_DESTROYED:  return "ELEMENT_DESTROYED";
        case TAURUS_EVENT_ATTRIBUTE_SET:      return "ATTRIBUTE_SET";
        case TAURUS_EVENT_ATTRIBUTE_REMOVED:  return "ATTRIBUTE_REMOVED";
        case TAURUS_EVENT_TEXT_CHANGED:       return "TEXT_CHANGED";
        case TAURUS_EVENT_NAME_CHANGED:       return "NAME_CHANGED";
        case TAURUS_EVENT_NAMESPACE_ADDED:    return "NAMESPACE_ADDED";
        case TAURUS_EVENT_NAMESPACE_REMOVED:  return "NAMESPACE_REMOVED";
        case TAURUS_EVENT_DOCUMENT_CLEARED:   return "DOCUMENT_CLEARED";
        case TAURUS_EVENT_DOCUMENT_FREEING:   return "DOCUMENT_FREEING";
        default:                              return "UNKNOWN";
    }
}

/* ============================================================================
 * Document Integration
 * ============================================================================ */

/**
 * Initialize observer list for a document (called during document creation).
 */
void taurus_observer_init_document(struct taurus_document* doc) {
    if (!doc) return;
    doc->observer_list = NULL;  /* Lazy initialization on first observer */
}

/**
 * Cleanup observer list for a document (called during document free).
 */
void taurus_observer_cleanup_document(struct taurus_document* doc) {
    if (!doc) return;

    /* Emit document freeing event */
    taurus_emit_event(doc, TAURUS_EVENT_DOCUMENT_FREEING, NULL, NULL, NULL,
                      NULL, NULL, NULL);

    /* Free observer list */
    if (doc->observer_list) {
        observer_list_free((ObserverList*)doc->observer_list);
        doc->observer_list = NULL;
    }
}
