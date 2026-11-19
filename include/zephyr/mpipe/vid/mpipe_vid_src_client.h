/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video source client: a video source whose producer runs on another core.
 * @ingroup mpipe_vid_src_clients
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SRC_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SRC_CLIENT_H_

/**
 * @defgroup mpipe_vid_src_clients Source Clients
 * @ingroup mpipe_vid
 * @brief Client-side video source element.
 * @{
 */

#include <stdint.h>

#include <zephyr/mpipe/mpipe_src_client.h>

#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_rpc.h>

/**
 * @brief Video source client element structure
 *
 * A source of video frames whose producer runs on another core. The formats
 * the element offers are those the server reports, and its buffers come from
 * the video buffer heap, which the board places in memory both cores reach.
 */
struct mpipe_vid_src_client {
	/** Base source client structure */
	struct mpipe_src_client src;
	/** Pool of the frames the server fills */
	struct mpipe_vid_buffer_pool_client pool;
	/** Format calls to the server */
	const struct mpipe_vid_rpc_format_api *format;
	/** What gives a free buffer of the pool, before the server fills it */
	int (*pool_acquire)(struct mpipe_buffer_pool *pool, struct net_buf **buf);
};

/**
 * @brief Initialize a video source client element
 *
 * The element calls its server through @p vid_channel, which an RPC backend
 * or the application provides. The channel is opened during the call, so a
 * server that cannot be reached is reported through the return value.
 *
 * @param vsrc Pointer to the video source client.
 * @param id Element identifier.
 * @param vid_channel The channel and the format calls of the element. The
 *                    structure is copied; the channel must remain valid.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_src_client_init(struct mpipe_vid_src_client *vsrc, uint8_t id,
			      const struct mpipe_vid_rpc_channel *vid_channel);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SRC_CLIENT_H_ */
