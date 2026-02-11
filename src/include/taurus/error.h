/* libtaurus - Error Handling
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains error handling utilities and error code descriptions.
 */

#ifndef TAURUS_ERROR_H
#define TAURUS_ERROR_H

#include "types.h"

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
const char* taurus_error_message(TaurusStatus status);

/**
 * Get last error message from the library
 *
 * @return Error message string (static, do not free)
 */
const char* taurus_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ERROR_H */
