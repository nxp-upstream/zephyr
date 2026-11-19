/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Client-side video transform element for multi-core pipelines.
 * @ingroup mpipe_vid_transform_clients
 *
 * Extends the generic transform client with video-specific RPC callbacks
 * to offload video processing to a remote core.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_TRANSFORM_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_TRANSFORM_CLIENT_H_

/**
 * @defgroup mpipe_vid_transform_clients Transform Clients
 * @ingroup mpipe_vid
 * @brief Client-side video transform elements.
 * @{
 */

#include <zephyr/drivers/video.h>

#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_transform_client.h>

#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_rpc.h>

struct mpipe_element;

/**
 * @brief Client-side video transform element structure.
 *
 * Extends @ref mpipe_transform_client with video buffer pools and with the
 * format calls to the server: capability query, format negotiation and
 * capability transformation.
 */
struct mpipe_vid_transform_client {
	/** Base transform client structure */
	struct mpipe_transform_client transform;
	/** Input buffer pool */
	struct mpipe_vid_buffer_pool_client in_pool;
	/** Output buffer pool */
	struct mpipe_vid_buffer_pool_client out_pool;
	/** Format calls to the server, set before the element is initialized */
	const struct mpipe_vid_rpc_format_api *format;
};

/**
 * @brief Initialize a client-side video transform element.
 *
 * The channel of its @ref mpipe_transform_client base and the format calls of
 * the element must be set when this is called.
 *
 * @param vtc Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_transform_client_init(struct mpipe_vid_transform_client *vtc, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_TRANSFORM_CLIENT_H_ */
