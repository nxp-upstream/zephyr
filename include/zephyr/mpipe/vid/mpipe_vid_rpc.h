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

#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/sys/util.h>

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

/**
 * @brief What a video client element calls its server through
 *
 * An RPC backend gives a video client element both: the channel for the
 * buffers, the states and the end of stream, and the format calls that go with
 * that channel.
 */
struct mpipe_vid_rpc_channel {
	/** Channel to the server */
	struct mpipe_rpc_channel *channel;
	/** Format calls of the backend the channel is from */
	const struct mpipe_vid_rpc_format_api *format;
};

struct mpipe_vid_dt_channel;

/**
 * @brief Function giving the video channel of a devicetree channel node
 *
 * @param dt Entry of the channel node
 * @param[out] vid_channel The video channel
 *
 * @retval 0 Success
 * @retval -errno The backend has no channel left
 */
typedef int (*mpipe_vid_dt_channel_get_t)(const struct mpipe_vid_dt_channel *dt,
					  struct mpipe_vid_rpc_channel *vid_channel);

/**
 * @brief Entry of a channel node of the devicetree, for the video client elements
 *
 * A video client element described in the devicetree names its channel with
 * the @c channel property, a node of an RPC backend. The backend defines one
 * entry per such node with @ref MPIPE_VID_DT_CHANNEL_DEFINE.
 */
struct mpipe_vid_dt_channel {
	/** Gives the video channel of the node */
	mpipe_vid_dt_channel_get_t get;
	/** Properties of the node, in the form the backend defines */
	const void *config;
};

/**
 * @brief Name of the entry of a channel node
 *
 * @param node_id Node of the channel
 */
#define MPIPE_VID_DT_CHANNEL_NAME(node_id) _CONCAT(mpipe_vid_dt_channel_, DT_DEP_ORD(node_id))

/**
 * @brief Define the entry of a channel node for the video client elements
 *
 * @param node_id Node of the channel
 * @param get_fn Function giving the video channel, an
 *               @ref mpipe_vid_dt_channel_get_t
 * @param config_ptr Properties of the node handed to @p get_fn, or NULL
 */
#define MPIPE_VID_DT_CHANNEL_DEFINE(node_id, get_fn, config_ptr)                                   \
	const struct mpipe_vid_dt_channel MPIPE_VID_DT_CHANNEL_NAME(node_id) = {                   \
		.get = (get_fn),                                                                   \
		.config = (config_ptr),                                                            \
	}

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_RPC_H_ */
