/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_object.h>
#include <zephyr/mpipe/vid/mpipe_vid_src_client.h>

LOG_MODULE_REGISTER(mpipe_vid_src_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_vid_src_client_enum_caps(struct mpipe_pad *pad, uint32_t index,
					  const struct mpipe_structure *filter,
					  struct mpipe_structure *out)
{
	struct mpipe_vid_src_client *vsrc = (struct mpipe_vid_src_client *)pad->object.container;
	struct mpipe_structure candidate;
	struct video_format_cap vfc;
	int ret;

	if (index > UINT8_MAX || vsrc->format->get_format_caps(vsrc->src.channel, MPIPE_PAD_SRC,
							       (uint8_t)index, &vfc) != 0) {
		return -ENOENT;
	}

	ret = mpipe_vid_vfc_to_caps(&vfc, &candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_vid_src_client_set_caps(struct mpipe_src *src, const struct mpipe_structure *caps)
{
	struct mpipe_vid_src_client *vsrc = (struct mpipe_vid_src_client *)src;
	struct video_format fmt;
	int ret;

	if (caps == NULL || !mpipe_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	ret = mpipe_vid_caps_to_format(caps, VIDEO_BUF_TYPE_OUTPUT, &fmt);
	if (ret < 0) {
		return ret;
	}

	/* The server negotiates its pipeline with this format */
	ret = vsrc->format->set_format(vsrc->src.channel, &fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format: pixfmt=%u w=%u h=%u", fmt.pixelformat, fmt.width,
			fmt.height);
		return ret;
	}

	vsrc->pool.pool.config.size = fmt.size;

	return mpipe_pad_set_caps(&src->src_pad, caps);
}

/* A free buffer of the pool, filled by the server */
static int mpipe_vid_src_client_acquire_buffer(struct mpipe_buffer_pool *pool, struct net_buf **buf)
{
	struct mpipe_vid_src_client *vsrc =
		CONTAINER_OF(pool, struct mpipe_vid_src_client, pool.pool);
	int ret;

	ret = vsrc->pool_acquire(pool, buf);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_src_client_fill(&vsrc->src, *buf);
	if (ret != 0) {
		net_buf_unref(*buf);
		*buf = NULL;
	}

	return ret;
}

int mpipe_vid_src_client_init(struct mpipe_vid_src_client *vsrc, uint8_t id,
			      const struct mpipe_vid_rpc_channel *vid_channel)
{
	__ASSERT_NO_MSG(vsrc != NULL);
	__ASSERT_NO_MSG(vid_channel != NULL && vid_channel->channel != NULL &&
			vid_channel->format != NULL);

	struct mpipe_element *self = &vsrc->src.src.element;
	struct mpipe_src *src = &vsrc->src.src;
	struct mpipe_buffer_pool_config req = {0};
	int ret;

	vsrc->src.channel = vid_channel->channel;
	vsrc->format = vid_channel->format;

	ret = mpipe_src_client_init(&vsrc->src, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_src_client");

	src->src_pad.enum_caps_fn = mpipe_vid_src_client_enum_caps;
	src->set_caps = mpipe_vid_src_client_set_caps;

	src->pool = &vsrc->pool.pool;
	mpipe_vid_buffer_pool_client_init(src->pool);
	vsrc->pool_acquire = src->pool->acquire_buffer;
	src->pool->acquire_buffer = mpipe_vid_src_client_acquire_buffer;

	/* Asked for once; the formats are enumerated from the server on demand */
	if (vsrc->format->get_buf_caps(vsrc->src.channel, MPIPE_PAD_SRC, &req.min_buffers,
				       &req.align) != 0) {
		LOG_ERR("Unable to retrieve buffer pool capabilities");
		return -ENODEV;
	}

	return mpipe_buffer_pool_set_req_config(src->pool, &req);
}
