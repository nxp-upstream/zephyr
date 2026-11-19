/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Format calls of the video client elements.
 * @ingroup mpipe_vid
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_RPC_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_RPC_H_

/**
 * @addtogroup mpipe_vid
 * @{
 */

#include <stdint.h>

#include <zephyr/drivers/video.h>

#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_rpc_channel.h>

/**
 * @brief Format calls a video client element makes to its server
 *
 * The video client elements negotiate with the types of the video API. An RPC
 * backend implements these functions once for all of them; each takes the
 * channel of the element that calls.
 */
struct mpipe_vid_rpc_format_api {
	/**
	 * @brief Query the buffer pool requirements of the server
	 *
	 * @param channel Channel of the client element
	 * @param direction Pad the buffers go through, see @ref mpipe_pad_direction
	 * @param[out] min_buffers Minimum number of buffers required
	 * @param[out] buf_align Required buffer alignment
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*get_buf_caps)(struct mpipe_rpc_channel *channel, enum mpipe_pad_direction direction,
			    uint8_t *min_buffers, uint16_t *buf_align);
	/**
	 * @brief Enumerate the video formats the server supports
	 *
	 * @param channel Channel of the client element
	 * @param direction Pad the formats are those of
	 * @param ind Index to iterate through the formats
	 * @param[out] vfc Video format capability at @p ind
	 *
	 * @return 0 on success, a negative errno on failure or past the last format
	 */
	int (*get_format_caps)(struct mpipe_rpc_channel *channel,
			       enum mpipe_pad_direction direction, uint8_t ind,
			       struct video_format_cap *vfc);
	/**
	 * @brief Set a video format
	 *
	 * @param channel Channel of the client element
	 * @param[in,out] fmt Video format to apply; the server fills in the pitch and size
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*set_format)(struct mpipe_rpc_channel *channel, struct video_format *fmt);
	/**
	 * @brief Transform a capability to the opposite pad, transform clients only
	 *
	 * @param channel Channel of the client element
	 * @param direction Pad to transform from
	 * @param ind Index to iterate through the transformed capabilities
	 * @param vfc Input video format capability
	 * @param[out] other_vfc Transformed video format capability
	 *
	 * @return 0 on success, a negative errno on failure or past the last capability
	 */
	int (*transform_cap)(struct mpipe_rpc_channel *channel, enum mpipe_pad_direction direction,
			     uint16_t ind, const struct video_format_cap *vfc,
			     struct video_format_cap *other_vfc);
};

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_RPC_H_ */
