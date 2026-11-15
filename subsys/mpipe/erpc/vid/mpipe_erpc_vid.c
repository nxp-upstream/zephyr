/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/video.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/erpc/mpipe_erpc.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>
#include <zephyr/mpipe/mpipe_pad.h>

#include "mpipe_stream_common.h"
#include "mpipe_erpc_vid_objects.h"

/* The direction crosses the wire as its numeric value */
BUILD_ASSERT((int)MPIPE_RPC_PAD_UNKNOWN == (int)MPIPE_PAD_UNKNOWN);
BUILD_ASSERT((int)MPIPE_RPC_PAD_SRC == (int)MPIPE_PAD_SRC);
BUILD_ASSERT((int)MPIPE_RPC_PAD_SINK == (int)MPIPE_PAD_SINK);

static int mpipe_erpc_vid_format_get_buf_caps(struct mpipe_rpc_channel *channel,
					      enum mpipe_pad_direction direction,
					      uint8_t *min_buffers, uint16_t *buf_align)
{
	uint32_t instance;
	erpc_client_t client = mpipe_erpc_channel_lock(channel, &instance);
	int32_t ret;

	ret = mpipe_erpc_vid_objects_get_buf_caps(client, instance, (uint8_t)direction, min_buffers,
						  buf_align);
	mpipe_erpc_channel_unlock(channel);

	return ret;
}

static int mpipe_erpc_vid_format_get_format_caps(struct mpipe_rpc_channel *channel,
						 enum mpipe_pad_direction direction, uint8_t ind,
						 struct video_format_cap *vfc)
{
	mpipe_rpc_video_format_cap rpc;
	uint32_t instance;
	erpc_client_t client = mpipe_erpc_channel_lock(channel, &instance);
	int32_t ret;

	ret = mpipe_erpc_vid_objects_get_caps(client, instance, (uint8_t)direction, ind, &rpc);
	mpipe_erpc_channel_unlock(channel);
	if (ret != 0) {
		return ret;
	}

	vfc->pixelformat = rpc.pixelformat;
	vfc->width_min = rpc.width_min;
	vfc->width_max = rpc.width_max;
	vfc->height_min = rpc.height_min;
	vfc->height_max = rpc.height_max;
	vfc->width_step = rpc.width_step;
	vfc->height_step = rpc.height_step;

	return 0;
}

static int mpipe_erpc_vid_format_set_format(struct mpipe_rpc_channel *channel,
					    struct video_format *fmt)
{
	mpipe_rpc_video_format rpc = {
		.type = (uint8_t)fmt->type,
		.pixelformat = fmt->pixelformat,
		.width = fmt->width,
		.height = fmt->height,
		.pitch = fmt->pitch,
		.size = fmt->size,
	};
	uint32_t instance;
	erpc_client_t client = mpipe_erpc_channel_lock(channel, &instance);
	int32_t ret;

	ret = mpipe_erpc_vid_objects_set(client, instance, &rpc);
	mpipe_erpc_channel_unlock(channel);
	if (ret != 0) {
		return ret;
	}

	/* The server fills in what the format implies, the pitch and size above all */
	fmt->pixelformat = rpc.pixelformat;
	fmt->width = rpc.width;
	fmt->height = rpc.height;
	fmt->pitch = rpc.pitch;
	fmt->size = rpc.size;

	return 0;
}

static int mpipe_erpc_vid_format_transform_cap(struct mpipe_rpc_channel *channel,
					       enum mpipe_pad_direction direction, uint16_t ind,
					       const struct video_format_cap *vfc,
					       struct video_format_cap *other_vfc)
{
	mpipe_rpc_video_format_cap rpc = {
		.pixelformat = vfc->pixelformat,
		.width_min = vfc->width_min,
		.width_max = vfc->width_max,
		.height_min = vfc->height_min,
		.height_max = vfc->height_max,
		.width_step = vfc->width_step,
		.height_step = vfc->height_step,
	};
	mpipe_rpc_video_format_cap other_rpc;
	uint32_t instance;
	erpc_client_t client = mpipe_erpc_channel_lock(channel, &instance);
	int32_t ret;

	ret = mpipe_erpc_vid_objects_transform_cap(client, instance, (uint8_t)direction, ind, &rpc,
						   &other_rpc);
	mpipe_erpc_channel_unlock(channel);
	if (ret != 0) {
		return ret;
	}

	other_vfc->pixelformat = other_rpc.pixelformat;
	other_vfc->width_min = other_rpc.width_min;
	other_vfc->width_max = other_rpc.width_max;
	other_vfc->height_min = other_rpc.height_min;
	other_vfc->height_max = other_rpc.height_max;
	other_vfc->width_step = other_rpc.width_step;
	other_vfc->height_step = other_rpc.height_step;

	return 0;
}

static const struct mpipe_vid_rpc_format_api mpipe_erpc_vid_format_api = {
	.get_buf_caps = mpipe_erpc_vid_format_get_buf_caps,
	.get_format_caps = mpipe_erpc_vid_format_get_format_caps,
	.set_format = mpipe_erpc_vid_format_set_format,
	.transform_cap = mpipe_erpc_vid_format_transform_cap,
};

int mpipe_erpc_vid_channel_get(const char *ept_name, uint32_t function,
			       struct mpipe_vid_rpc_channel *vid_channel)
{
	__ASSERT_NO_MSG(vid_channel != NULL);

	vid_channel->channel = mpipe_erpc_channel_get(ept_name, function);
	if (vid_channel->channel == NULL) {
		return -ENOMEM;
	}

	vid_channel->format = &mpipe_erpc_vid_format_api;

	return 0;
}

#define DT_DRV_COMPAT zephyr_mpipe_erpc_channel

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
struct mpipe_erpc_vid_dt_config {
	const char *endpoint;
	uint32_t function;
};

static int mpipe_erpc_vid_dt_channel_get(const struct mpipe_vid_dt_channel *dt,
					 struct mpipe_vid_rpc_channel *vid_channel)
{
	const struct mpipe_erpc_vid_dt_config *config = dt->config;

	return mpipe_erpc_vid_channel_get(config->endpoint, config->function, vid_channel);
}

/* An entry for each channel node: the one a video client element names is used */
#define MPIPE_ERPC_VID_DT_CHANNEL_DEFINE(inst)                                                     \
	static const struct mpipe_erpc_vid_dt_config mpipe_erpc_vid_dt_config_##inst = {           \
		.endpoint = DT_INST_PROP(inst, endpoint),                                          \
		.function = DT_INST_PROP(inst, function),                                          \
	};                                                                                         \
	MPIPE_VID_DT_CHANNEL_DEFINE(DT_DRV_INST(inst), mpipe_erpc_vid_dt_channel_get,              \
				    &mpipe_erpc_vid_dt_config_##inst);

DT_INST_FOREACH_STATUS_OKAY(MPIPE_ERPC_VID_DT_CHANNEL_DEFINE)
#endif
