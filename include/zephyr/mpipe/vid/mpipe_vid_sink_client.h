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
	/**
	 * @brief RPC callback to query buffer pool capabilities.
	 *
	 * @param min_buffers  Output: minimum number of buffers required.
	 * @param buf_align    Output: required buffer alignment.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*get_buf_caps_rpc)(uint8_t *min_buffers, uint16_t *buf_align);
	/**
	 * @brief RPC callback to enumerate the accepted video formats.
	 *
	 * @param ind       Index to iterate through available capabilities.
	 * @param[out] vfc  Output: video format capability at @p ind.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*get_format_caps_rpc)(uint8_t ind, struct video_format_cap *vfc);
	/**
	 * @brief RPC callback to set the video format.
	 *
	 * @param[in,out] fmt Video format to apply; the server may update size and pitch.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*set_format_rpc)(struct video_format *fmt);
};

/**
 * @brief Initialize a video sink client element
 *
 * The RPC hooks of the element and of its @ref mpipe_sink_client base must be
 * set when this is called.
 *
 * @param vsc Pointer to the video sink client.
 * @param id Element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_sink_client_init(struct mpipe_vid_sink_client *vsc, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_SINK_CLIENT_H_ */
