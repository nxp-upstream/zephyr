/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/erpc/mpipe_erpc.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/vid/mpipe_vid_pipeline_server.h>
#include <zephyr/mpipe/erpc/mpipe_erpc_vid.h>

#include "mpipe_stream_common.h"
#include "mpipe_erpc_vid_objects.h"

/* The direction crosses the wire as its numeric value */
BUILD_ASSERT((int)MPIPE_RPC_PAD_UNKNOWN == (int)MPIPE_PAD_UNKNOWN);
BUILD_ASSERT((int)MPIPE_RPC_PAD_SRC == (int)MPIPE_PAD_SRC);
BUILD_ASSERT((int)MPIPE_RPC_PAD_SINK == (int)MPIPE_PAD_SINK);

LOG_MODULE_REGISTER(mpipe_erpc_vid_server, CONFIG_MPIPE_LOG_LEVEL);

static void vfc_to_rpc(const struct video_format_cap *vfc, mpipe_rpc_video_format_cap *rpc)
{
	rpc->pixelformat = vfc->pixelformat;
	rpc->width_min = vfc->width_min;
	rpc->width_max = vfc->width_max;
	rpc->height_min = vfc->height_min;
	rpc->height_max = vfc->height_max;
	rpc->width_step = vfc->width_step;
	rpc->height_step = vfc->height_step;
}

static int32_t get_buf_caps(void *ctx, uint32_t instance, uint8_t direction, uint8_t *min_buffers,
			    uint16_t *buf_align)
{
	struct mpipe_pipeline_server *server = mpipe_erpc_server_get_pipeline(ctx, instance);

	if (server == NULL) {
		return -EINVAL;
	}

	return mpipe_vid_pipeline_server_get_buf_caps(server, (enum mpipe_pad_direction)direction,
						      min_buffers, buf_align);
}

static int32_t get_caps(void *ctx, uint32_t instance, uint8_t direction, uint8_t ind,
			mpipe_rpc_video_format_cap *vfc)
{
	struct mpipe_pipeline_server *server = mpipe_erpc_server_get_pipeline(ctx, instance);
	struct video_format_cap cap;
	int ret;

	if (server == NULL) {
		return -EINVAL;
	}

	ret = mpipe_vid_pipeline_server_get_format_caps(server, (enum mpipe_pad_direction)direction,
							ind, &cap);
	if (ret != 0) {
		return ret;
	}

	vfc_to_rpc(&cap, vfc);

	return 0;
}

static int32_t set_format(void *ctx, uint32_t instance, mpipe_rpc_video_format *rpc)
{
	struct mpipe_pipeline_server *server = mpipe_erpc_server_get_pipeline(ctx, instance);
	struct video_format fmt = {
		.type = (enum video_buf_type)rpc->type,
		.pixelformat = rpc->pixelformat,
		.width = rpc->width,
		.height = rpc->height,
	};
	int ret;

	if (server == NULL) {
		return -EINVAL;
	}

	ret = mpipe_vid_pipeline_server_set_format(server, &fmt);
	if (ret != 0) {
		return ret;
	}

	rpc->pitch = fmt.pitch;
	rpc->size = fmt.size;

	return 0;
}

static int32_t transform_cap(void *ctx, uint32_t instance, uint8_t direction, uint16_t ind,
			     const mpipe_rpc_video_format_cap *vfc,
			     mpipe_rpc_video_format_cap *other_vfc)
{
	struct mpipe_pipeline_server *server = mpipe_erpc_server_get_pipeline(ctx, instance);
	struct video_format_cap cap = {
		.pixelformat = vfc->pixelformat,
		.width_min = vfc->width_min,
		.width_max = vfc->width_max,
		.height_min = vfc->height_min,
		.height_max = vfc->height_max,
		.width_step = vfc->width_step,
		.height_step = vfc->height_step,
	};
	struct video_format_cap other;
	int ret;

	if (server == NULL) {
		return -EINVAL;
	}

	ret = mpipe_vid_pipeline_server_transform_cap(server, (enum mpipe_pad_direction)direction,
						      ind, &cap, &other);
	if (ret != 0) {
		return ret;
	}

	vfc_to_rpc(&other, other_vfc);

	return 0;
}

int mpipe_erpc_vid_server_run(struct mpipe_pipeline_server *const *servers, uint32_t num_servers,
			      const char *ept_name)
{
	static const struct mpipe_erpc_vid_objects_handler handler = {
		.get_buf_caps = get_buf_caps,
		.get_caps = get_caps,
		.set = set_format,
		.transform_cap = transform_cap,
	};
	struct mpipe_erpc_server *erpc_server;
	void *service;

	__ASSERT_NO_MSG(servers != NULL && num_servers > 0U);
	__ASSERT_NO_MSG(ept_name != NULL);

	/* Returns once a client element has bound the endpoint */
	erpc_server = mpipe_erpc_server_create(servers, num_servers, ept_name);
	if (erpc_server == NULL) {
		return -EIO;
	}

	service = mpipe_erpc_vid_objects_service_create(&handler, erpc_server);
	if (service == NULL) {
		LOG_ERR("No video format service left for %s", ept_name);
		return -ENOMEM;
	}

	mpipe_erpc_server_add_service(erpc_server, service);

	LOG_INF("Serving %s", ept_name);

	return mpipe_erpc_server_run(erpc_server);
}
