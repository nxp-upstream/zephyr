/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief eRPC backend for the client elements.
 * @ingroup mpipe_erpc
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_
#define ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_

/**
 * @defgroup mpipe_erpc eRPC backend
 * @ingroup mpipe
 * @brief eRPC transport for the client elements and their servers.
 *
 * A client element calls a server on another core through its RPC channel,
 * see @ref mpipe_rpc_channel. This backend provides channels over eRPC, with
 * the Zephyr IPC service as the transport, and the server that answers them
 * with a pipeline server.
 *
 * Each client element has a channel of its own, so a pipeline can hold several
 * client elements of the same kind. A channel names an endpoint. Client
 * elements on different endpoints never wait for each other; client elements
 * on the same endpoint share one server, which then needs one thread only and
 * tells them apart by an instance number. The eRPC objects are static.
 *
 * The backend carries the calls every client element makes. A plugin adds the
 * calls of its media type, through the eRPC client of the channel and through
 * a service it adds to the server.
 *
 * @{
 */

#include <stdint.h>

#include <zephyr/mpipe/mpipe_rpc_channel.h>

#include "erpc_client_setup.h"
#include "erpc_common.h"
#include "erpc_server_setup.h"

struct mpipe_pipeline_server;

/**
 * @brief Get an eRPC channel for a client element.
 *
 * Channels that name the same endpoint share its eRPC client: their calls go
 * to one server, which answers them one at a time, and each channel has an
 * instance of its own there. Channels on different endpoints do not wait for
 * each other.
 *
 * The channel is connected when the element opens it: that call returns once
 * the server on the other core has bound the endpoint and opened the function.
 *
 * @param ept_name Name of the endpoint, the one the server registers. The
 *                 string must remain valid.
 * @param function What the element runs on the server, a number the server
 *                 defines. A pipeline server knows function 0 only.
 *
 * @return The channel, or NULL when @kconfig{CONFIG_MPIPE_ERPC_CHANNELS_NUM}
 *         channels or @kconfig{CONFIG_MPIPE_ERPC_ENDPOINTS_NUM} endpoints are
 *         taken already.
 */
struct mpipe_rpc_channel *mpipe_erpc_channel_get(const char *ept_name, uint32_t function);

/**
 * @brief Take the eRPC client of a channel for one call.
 *
 * A plugin calls the functions of its own interface between this call and
 * mpipe_erpc_channel_unlock(): the client makes one call at a time, and
 * several channels may share it.
 *
 * @param channel A channel from mpipe_erpc_channel_get(), open.
 * @param[out] instance Instance of the channel on the server, to pass with the
 *                      call. May be NULL.
 *
 * @return The eRPC client.
 */
erpc_client_t mpipe_erpc_channel_lock(struct mpipe_rpc_channel *channel, uint32_t *instance);

/**
 * @brief Release the eRPC client of a channel.
 *
 * @param channel The channel given to mpipe_erpc_channel_lock().
 */
void mpipe_erpc_channel_unlock(struct mpipe_rpc_channel *channel);

/**
 * @brief Log an eRPC error.
 *
 * Installed on the eRPC client of every endpoint.
 *
 * @param err eRPC status to report; kErpcStatus_Success logs nothing.
 * @param function_id Identifier of the RPC function the error occurred in,
 *                    or 0 when the error is not bound to one.
 */
void mpipe_erpc_error_handler(erpc_status_t err, uint32_t function_id);

/** An eRPC server and the pipeline servers it answers with */
struct mpipe_erpc_server;

/**
 * @brief Create an eRPC server that answers with pipeline servers.
 *
 * The server answers the calls every client element makes. A client element
 * opens function @c n of the endpoint to reach @p pipelines[n]. One thread
 * serves every pipeline of the endpoint, one call at a time: pipelines that
 * must work at the same time take a server and an endpoint each.
 *
 * The call returns once a client on the other core has bound the endpoint.
 *
 * Available with @kconfig{CONFIG_MPIPE_ERPC_SERVER}.
 *
 * @param pipelines The pipeline servers, which must remain valid.
 * @param num_pipelines Number of pipeline servers.
 * @param ept_name Name of the endpoint, the one the client elements use.
 *
 * @return The server, or NULL when @kconfig{CONFIG_MPIPE_ERPC_SERVERS_NUM}
 *         servers are taken already or when the transport could not be
 *         initialized.
 */
struct mpipe_erpc_server *mpipe_erpc_server_create(struct mpipe_pipeline_server *const *pipelines,
						   uint32_t num_pipelines, const char *ept_name);

/**
 * @brief Get the pipeline server of an instance.
 *
 * For the interface of a plugin, whose calls carry the instance too.
 *
 * @param server A server from mpipe_erpc_server_create().
 * @param instance Instance a client element opened.
 *
 * @return The pipeline server, NULL when the endpoint has no such instance.
 */
struct mpipe_pipeline_server *mpipe_erpc_server_get_pipeline(struct mpipe_erpc_server *server,
							     uint32_t instance);

/**
 * @brief Add the service of a plugin's interface to an eRPC server.
 *
 * @param server A server from mpipe_erpc_server_create().
 * @param service The eRPC service object, which must remain valid.
 */
void mpipe_erpc_server_add_service(struct mpipe_erpc_server *server, void *service);

/**
 * @brief Answer the calls of the client elements.
 *
 * The calling thread serves the pipelines; it sleeps between two calls.
 *
 * @param server A server from mpipe_erpc_server_create().
 *
 * @retval -EIO The transport failed; the function does not return otherwise.
 */
int mpipe_erpc_server_run(struct mpipe_erpc_server *server);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_H_ */
