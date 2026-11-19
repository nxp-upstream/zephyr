/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video sink client.
 * @ingroup mpipe_vid_sink_clients
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SINK_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SINK_CLIENT_H_

/**
 * @defgroup mpipe_vid_sink_clients Sink Clients
 * @ingroup mpipe_vid
 * @brief Video sinks whose consumer runs on another core.
 * @{
 */

#include <zephyr/drivers/video.h>

#include <zephyr/mpipe/mpipe_sink_client.h>

#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_rpc.h>

/**
 * @brief Video sink client element structure
 *
 * A sink for video frames whose consumer runs on another core. The formats
 * the element accepts are those the server reports, and its buffers come
 * from the video buffer heap, which the board places in memory both cores
 * reach.
 */
struct mpipe_vid_sink_client {
	/** Base sink client structure */
	struct mpipe_sink_client sink;
	/** Input buffer pool */
	struct mpipe_vid_buffer_pool_client in_pool;
	/** Format calls to the server */
	const struct mpipe_vid_rpc_format_api *format;
};

/**
 * @brief Initialize a video sink client element
 *
 * The element calls its server through @p vid_channel, which an RPC backend
 * or the application provides. The channel is opened during the call, so a
 * server that cannot be reached is reported through the return value.
 *
 * @param vsc Pointer to the video sink client.
 * @param id Element identifier.
 * @param vid_channel The channel and the format calls of the element. The
 *                    structure is copied; the channel must remain valid.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_sink_client_init(struct mpipe_vid_sink_client *vsc, uint8_t id,
			       const struct mpipe_vid_rpc_channel *vid_channel);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SINK_CLIENT_H_ */
