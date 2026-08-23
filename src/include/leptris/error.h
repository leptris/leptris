/* libleptris - Error Handling
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains error handling utilities and error code descriptions.
 */

#ifndef LEPTRIS_ERROR_H
#define LEPTRIS_ERROR_H

#include "types.h"

/* Export macro (mirrors dom/document.h). */
#if !defined(LEPTRIS_API)
#  if defined(_WIN32)
#    if defined(LEPTRIS_BUILDING_DLL)
#      define LEPTRIS_API __declspec(dllexport)
#    else
#      define LEPTRIS_API __declspec(dllimport)
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Message Functions
 * ============================================================================ */

/**
 * Get human-readable error message for status code
 *
 * @param status Status code
 * @return Error message string (static, do not free)
 */
LEPTRIS_API const char* leptris_error_message(LeptrisStatus status);

/**
 * Get last error message from the library
 *
 * @return Error message string (static, do not free)
 */
LEPTRIS_API const char* leptris_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_ERROR_H */
