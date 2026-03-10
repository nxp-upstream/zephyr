/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _EMBEDDED_RPC__ERROR_HANDLER_H_
#define _EMBEDDED_RPC__ERROR_HANDLER_H_

#include <stdint.h>

#include "erpc_common.h"

/**
 * @file
 * @brief eRPC error handler file.
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handles eRPC errors
 *
 * This function prints a description of occurred error and sets bool variable g_erpc_error_occurred
 * which is used for determining if error occurred in user application on client side.
 *
 * @param err The eRPC error status code to handle
 * @param functionID The function ID where the error occurred on the client side
 */
void erpc_error_handler(erpc_status_t err, uint32_t functionID);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* _EMBEDDED_RPC__ERROR_HANDLER_H_ */
