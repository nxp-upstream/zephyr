/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief eRPC backend for the transform clients.
 * @ingroup mpipe_erpc
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_
#define ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_

/**
 * @defgroup mpipe_erpc eRPC backend
 * @ingroup mpipe
 * @brief eRPC transport for the transform clients.
 *
 * A transform client forwards its RPC hooks to a server on another core. This
 * backend brings up one eRPC client over the Zephyr IPC service and reports
 * the errors eRPC raises. Each plugin binds its own eRPC interface to the
 * client this backend returns.
 *
 * @{
 */

#include <stdint.h>

#include "erpc_client_setup.h"
#include "erpc_common.h"

/**
 * @brief Bring up the eRPC client.
 *
 * Initializes the IPC service transport, the message buffer factory and the
 * client, and installs mpipe_erpc_error_handler() on it. The client is
 * brought up once; later calls return the same one.
 *
 * @return The eRPC client, or NULL when the transport could not be
 *         initialized.
 */
erpc_client_t mpipe_erpc_client_init(void);

/**
 * @brief Log an eRPC error.
 *
 * Installed on the client by mpipe_erpc_client_init().
 *
 * @param err eRPC status to report; kErpcStatus_Success logs nothing.
 * @param function_id Identifier of the RPC function the error occurred in,
 *                    or 0 when the error is not bound to one.
 */
void mpipe_erpc_error_handler(erpc_status_t err, uint32_t function_id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_ */
