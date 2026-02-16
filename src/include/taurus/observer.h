/* observer.h - Document change observer interface
 * Copyright (c) 2024, Ribose Inc.
 *
 * This header provides an observer pattern implementation for tracking
 * document changes. This enables undo/redo systems, audit logging,
 * and reactive UI updates.
 *
 * Use cases:
 * - Implement undo/redo functionality
 * - Track document modifications for auditing
 * - Notify UI when DOM changes
 * - Build reactive XML editors
 *
 * Architectural Principle: Open/Closed
 * - Core DOM operations don't change (closed for modification)
 * - Observers can be added without touching core code (open for extension)
 *
 * Example usage:
 *
 *   void my_observer(const TaurusEvent* event, void* userdata) {
 *       printf("Event %d on element %s\n", event->type, taurus_element_name(event->target));
 *   }
 *
 *   int id = taurus_document_add_observer(doc, my_observer, NULL);
 *   // All DOM changes now trigger my_observer
 *   taurus_document_remove_observer(doc, id);
 */

#ifndef TAURUS_OBSERVER_H
#define TAURUS_OBSERVER_H

#include <stddef.h>

/* Export macro for Windows DLL support */
#ifndef TAURUS_API
#  ifdef _WIN32
#    ifdef TAURUS_BUILD_SHARED
#      define TAURUS_API __declspec(dllexport)
#    elif defined(TAURUS_USE_SHARED)
#      define TAURUS_API __declspec(dllimport)
#    else
#      define TAURUS_API
#    endif
#  else
#    define TAURUS_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct taurus_document;
struct taurus_element;

/* ============================================================================
 * Event Types
 * ============================================================================ */

/**
 * Event types emitted by document modifications.
 *
 * Each event type corresponds to a specific DOM modification operation.
 */
typedef enum {
    /* Element lifecycle events */
    TAURUS_EVENT_ELEMENT_CREATED = 1,     /* New element created (taurus_element_create) */
    TAURUS_EVENT_ELEMENT_ADDED = 2,       /* Element added to tree (append/prepend/insert) */
    TAURUS_EVENT_ELEMENT_REMOVED = 3,     /* Element removed from tree (remove_child) */
    TAURUS_EVENT_ELEMENT_DESTROYED = 4,   /* Element about to be freed */

    /* Attribute events */
    TAURUS_EVENT_ATTRIBUTE_SET = 10,      /* Attribute set/updated */
    TAURUS_EVENT_ATTRIBUTE_REMOVED = 11,  /* Attribute removed */

    /* Content events */
    TAURUS_EVENT_TEXT_CHANGED = 20,       /* Element text content changed */
    TAURUS_EVENT_NAME_CHANGED = 21,       /* Element name changed */

    /* Namespace events */
    TAURUS_EVENT_NAMESPACE_ADDED = 30,    /* Namespace declaration added */
    TAURUS_EVENT_NAMESPACE_REMOVED = 31,  /* Namespace declaration removed */

    /* Document events */
    TAURUS_EVENT_DOCUMENT_CLEARED = 40,   /* All children removed */
    TAURUS_EVENT_DOCUMENT_FREEING = 41    /* Document about to be freed */
} TaurusEventType;

/* ============================================================================
 * Event Structure
 * ============================================================================ */

/**
 * Event structure passed to observer callbacks.
 *
 * Contains all information about a DOM modification event.
 * The structure is only valid during the callback - copy any data you need.
 */
typedef struct {
    TaurusEventType type;           /* Event type */
    struct taurus_document* doc;    /* Document that emitted the event */
    struct taurus_element* target;  /* Element affected (may be NULL for document events) */
    struct taurus_element* parent;  /* Parent element (for ELEMENT_ADDED/REMOVED) */
    struct taurus_element* sibling; /* Related element (for insert_before/after) */

    /* Event-specific details */
    const char* name;               /* Attribute/name/namespace name */
    const char* old_value;          /* Previous value (for changes) */
    const char* new_value;          /* New value (for changes) */

    /* User data from observer registration */
    void* userdata;

    /* Reserved for future use */
    void* reserved[4];
} TaurusEvent;

/* ============================================================================
 * Observer Callback Type
 * ============================================================================ */

/**
 * Observer callback function type.
 *
 * @param event Event details (valid only during callback)
 * @param userdata User data passed during registration
 *
 * IMPORTANT: The event structure and its strings are only valid during
 * the callback. Copy any data you need to persist.
 *
 * WARNING: Do not modify the document during the callback, as this
 * may trigger recursive notifications and infinite loops.
 */
typedef void (*TaurusObserverCallback)(const TaurusEvent* event, void* userdata);

/* ============================================================================
 * Observer Management API
 * ============================================================================ */

/**
 * Add an observer to a document.
 *
 * The observer will receive callbacks for all DOM modification events
 * on this document.
 *
 * @param doc Document to observe
 * @param callback Callback function (must not be NULL)
 * @param userdata User data passed to callback (can be NULL)
 * @return Observer ID (positive integer) for later removal, or negative on error
 *
 * Thread Safety: Not thread-safe. Register observers before concurrent access.
 *
 * Example:
 *   int id = taurus_document_add_observer(doc, my_callback, my_data);
 *   // ... later ...
 *   taurus_document_remove_observer(doc, id);
 */
TAURUS_API int taurus_document_add_observer(
    struct taurus_document* doc,
    TaurusObserverCallback callback,
    void* userdata
);

/**
 * Remove an observer from a document.
 *
 * @param doc Document the observer was added to
 * @param observer_id Observer ID returned by taurus_document_add_observer
 * @return TAURUS_OK on success, TAURUS_ERROR_NOT_FOUND if observer not found
 */
TAURUS_API int taurus_document_remove_observer(
    struct taurus_document* doc,
    int observer_id
);

/**
 * Remove all observers from a document.
 *
 * @param doc Document to clear observers from
 * @return Number of observers removed
 */
TAURUS_API int taurus_document_clear_observers(struct taurus_document* doc);

/**
 * Check if a document has any observers.
 *
 * @param doc Document to check
 * @return 1 if document has observers, 0 if none or doc is NULL
 */
TAURUS_API int taurus_document_has_observers(struct taurus_document* doc);

/**
 * Get the number of observers on a document.
 *
 * @param doc Document to check
 * @return Number of observers, or 0 if doc is NULL
 */
TAURUS_API int taurus_document_observer_count(struct taurus_document* doc);

/* ============================================================================
 * Observer Group API (Advanced)
 * ============================================================================ */

/**
 * Observer group flags for filtering events.
 *
 * Use these flags to receive only specific event types.
 */
typedef enum {
    TAURUS_OBSERVE_ALL = 0,              /* Receive all events (default) */
    TAURUS_OBSERVE_ELEMENTS = 1 << 0,    /* Element lifecycle events */
    TAURUS_OBSERVE_ATTRIBUTES = 1 << 1,  /* Attribute events */
    TAURUS_OBSERVE_CONTENT = 1 << 2,     /* Content change events */
    TAURUS_OBSERVE_NAMESPACES = 1 << 3,  /* Namespace events */
    TAURUS_OBSERVE_DOCUMENT = 1 << 4     /* Document-level events */
} TaurusObserveFlags;

/**
 * Add a filtered observer to a document.
 *
 * The observer will only receive events matching the specified flags.
 *
 * @param doc Document to observe
 * @param callback Callback function
 * @param userdata User data passed to callback
 * @param flags Event type filter (combination of TaurusObserveFlags)
 * @return Observer ID for later removal, or negative on error
 *
 * Example:
 *   // Only observe attribute changes
 *   int id = taurus_document_add_observer_filtered(
 *       doc, my_callback, NULL, TAURUS_OBSERVE_ATTRIBUTES);
 */
TAURUS_API int taurus_document_add_observer_filtered(
    struct taurus_document* doc,
    TaurusObserverCallback callback,
    void* userdata,
    int flags
);

/* ============================================================================
 * Batch Operations
 * ============================================================================ */

/**
 * Suspend observer notifications for a document.
 *
 * While suspended, no events are emitted. This is useful for
 * batch operations that would trigger many notifications.
 *
 * @param doc Document to suspend notifications for
 *
 * Example:
 *   taurus_document_suspend_observers(doc);
 *   // Perform many modifications
 *   for (int i = 0; i < 1000; i++) {
 *       taurus_element_append_child(parent, children[i]);
 *   }
 *   taurus_document_resume_observers(doc);
 *   // Now observers receive a TAURUS_EVENT_DOCUMENT_CLEARED or similar
 */
TAURUS_API void taurus_document_suspend_observers(struct taurus_document* doc);

/**
 * Resume observer notifications for a document.
 *
 * @param doc Document to resume notifications for
 *
 * NOTE: Missed events are NOT replayed. Observers will not receive
 * notifications for modifications made while suspended.
 */
TAURUS_API void taurus_document_resume_observers(struct taurus_document* doc);

/**
 * Check if observer notifications are suspended.
 *
 * @param doc Document to check
 * @return 1 if suspended, 0 if active or doc is NULL
 */
TAURUS_API int taurus_document_observers_suspended(struct taurus_document* doc);

/* ============================================================================
 * Event Utilities
 * ============================================================================ */

/**
 * Get human-readable name for event type.
 *
 * @param type Event type
 * @return Static string describing the event type (do not free)
 */
TAURUS_API const char* taurus_event_type_name(TaurusEventType type);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_OBSERVER_H */
