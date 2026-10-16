/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video side of the eRPC backend.
 * @ingroup mpipe_erpc
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_VID_H_
#define ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_VID_H_

/**
 * @addtogroup mpipe_erpc
 * @{
 */

#include <stdint.h>

#include <zephyr/mpipe/vid/mpipe_vid_rpc.h>

struct mpipe_pipeline_server;

/**
 * @brief Get an eRPC channel for a video client element
 *
 * Gives the channel of mpipe_erpc_channel_get() together with the video
 * format calls over eRPC, for mpipe_vid_transform_client_init() and
 * mpipe_vid_sink_client_init(). Each client element takes a channel of its
 * own.
 *
 * Available with @kconfig{CONFIG_MPIPE_ERPC_VID}.
 *
 * @param ept_name Name of the IPC service endpoint, the one the server of
 *                 the element registers. The string must remain valid.
 * @param function What the element runs on the server, a number the server
 *                 defines. A pipeline server gives function @c n to the n-th
 *                 pipeline of its endpoint.
 * @param[out] vid_channel The video channel.
 *
 * @retval 0 Success
 * @retval -ENOMEM No eRPC channel or endpoint is left
 */
int mpipe_erpc_vid_channel_get(const char *ept_name, uint32_t function,
			       struct mpipe_vid_rpc_channel *vid_channel);

/**
 * @brief Serve pipelines to the video client elements of another core
 *
 * Registers the endpoint, waits for a client element to bind it and answers
 * the calls with the pipeline servers until the transport fails. A client
 * element opens function @c n of the endpoint to reach @p servers[n]. The
 * calling thread becomes the streaming thread of the served pipelines: give
 * it the stack and the priority that work needs.
 *
 * The function serves a video transform client, with a pipeline that has an
 * application sink, and a video sink client, with a pipeline that ends in a
 * sink of its own.
 *
 * The pipelines of one call are served one at a time. Pipelines that must
 * work at the same time are served from a thread and on an endpoint each, up
 * to @kconfig{CONFIG_MPIPE_ERPC_SERVERS_NUM} calls.
 *
 * Available with @kconfig{CONFIG_MPIPE_ERPC_VID_SERVER}.
 *
 * @param servers The pipeline servers, initialized. The array must remain
 *                valid.
 * @param num_servers Number of pipeline servers.
 * @param ept_name Name of the IPC service endpoint, the one the client
 *                 elements use. The string must remain valid.
 *
 * @retval -ENOMEM Every video format service is taken
 * @retval -EIO The transport could not be brought up, or failed
 */
int mpipe_erpc_vid_server_run(struct mpipe_pipeline_server *const *servers, uint32_t num_servers,
			      const char *ept_name);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_ERPC_MPIPE_ERPC_VID_H_ */
