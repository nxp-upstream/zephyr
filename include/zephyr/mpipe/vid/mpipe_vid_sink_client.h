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
	/** Format calls to the server, set before the element is initialized */
	const struct mpipe_vid_rpc_format_api *format;
};

/**
 * @brief Initialize a video sink client element
 *
 * The channel of its @ref mpipe_sink_client base and the format calls of the
 * element must be set when this is called.
 *
 * @param vsc Pointer to the video sink client.
 * @param id Element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_sink_client_init(struct mpipe_vid_sink_client *vsc, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SINK_CLIENT_H_ */
