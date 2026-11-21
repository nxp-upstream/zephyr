/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>
#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_object.h>
#include <zephyr/mpipe/vid/mpipe_vid_transform_client.h>

LOG_MODULE_REGISTER(mpipe_vid_transform_client, CONFIG_MPIPE_LOG_LEVEL);

/* Ask the remote for the pool parameters of one direction, once at init */
static int mpipe_vid_transform_client_probe_pool(struct mpipe_vid_transform_client *vtc,
						 enum mpipe_pad_direction direction,
						 struct mpipe_buffer_pool *pool)
{
	struct mpipe_buffer_pool_config req = {0};

	if (vtc->format->get_buf_caps(vtc->transform.channel, direction, &req.min_buffers,
				      &req.align) != 0) {
		LOG_ERR("Unable to retrieve buffer pool capabilities");
		return -ENODEV;
	}

	/*
	 * A buffer shares no cache line of this core with its neighbours, so
	 * the element can flush and invalidate it around a call.
	 */
	req.align = MAX(req.align, (uint16_t)MAX(sys_cache_data_line_size_get(), 1U));

	/* The floor every run starts from: a stop restores it */
	return mpipe_buffer_pool_set_req_config(pool, &req);
}

static int mpipe_vid_transform_client_enum_caps(struct mpipe_pad *pad, uint32_t index,
						const struct mpipe_structure *filter,
						struct mpipe_structure *out)
{
	struct mpipe_vid_transform_client *vtc =
		(struct mpipe_vid_transform_client *)pad->object.container;
	struct mpipe_structure candidate;
	struct video_format_cap vfc;
	int ret;

	if (pad->direction != MPIPE_PAD_SINK && pad->direction != MPIPE_PAD_SRC) {
		return -EINVAL;
	}

	if (vtc->format->get_format_caps(vtc->transform.channel, pad->direction, (uint8_t)index,
					 &vfc) != 0) {
		return -ENOENT;
	}

	ret = mpipe_vid_vfc_to_caps(&vfc, &candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_vid_transform_client_set_caps(struct mpipe_transform *transform,
					       enum mpipe_pad_direction direction,
					       const struct mpipe_structure *caps)
{
	struct mpipe_vid_transform_client *vtc = (struct mpipe_vid_transform_client *)transform;
	struct mpipe_buffer_pool *pool = NULL;
	struct video_format fmt;
	enum video_buf_type type;
	int ret;

	if (caps == NULL || !mpipe_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	if (direction == MPIPE_PAD_SINK) {
		type = VIDEO_BUF_TYPE_INPUT;
		pool = transform->in_pool;
	} else if (direction == MPIPE_PAD_SRC) {
		type = VIDEO_BUF_TYPE_OUTPUT;
		pool = transform->out_pool;
	} else {
		LOG_ERR("Pad direction is invalid");
		return -EINVAL;
	}

	ret = mpipe_vid_caps_to_format(caps, type, &fmt);
	if (ret < 0) {
		return ret;
	}

	ret = vtc->format->set_format(vtc->transform.channel, &fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format: type=%u pixfmt=%u w=%u h=%u", fmt.type,
			fmt.pixelformat, fmt.width, fmt.height);
		return ret;
	}

	/* Whole cache lines of this core, see mpipe_vid_transform_client_probe_pool() */
	pool->config.size = ROUND_UP(fmt.size, MAX(sys_cache_data_line_size_get(), 1U));

	/* Set pad's caps only when everything is OK */
	return mpipe_pad_set_caps(
		direction == MPIPE_PAD_SRC ? &transform->src_pad : &transform->sink_pad, caps);
}

static int mpipe_vid_transform_client_transform_caps(struct mpipe_transform *self,
						     enum mpipe_pad_direction direction,
						     const struct mpipe_structure *in,
						     uint32_t index, struct mpipe_structure *out)
{
	struct mpipe_vid_transform_client *vtc = (struct mpipe_vid_transform_client *)self;
	struct video_format_cap vfc, other_vfc;

	if (direction != MPIPE_PAD_SINK && direction != MPIPE_PAD_SRC) {
		return -EINVAL;
	}

	if (in == NULL || mpipe_vid_caps_to_vfc(in, &vfc) < 0) {
		return -ENOENT;
	}

	if (vtc->format->transform_cap(vtc->transform.channel, direction, (uint16_t)index, &vfc,
				       &other_vfc) != 0) {
		return -ENOENT;
	}

	return mpipe_vid_vfc_to_caps(&other_vfc, out);
}

int mpipe_vid_transform_client_init(struct mpipe_vid_transform_client *vtc, uint8_t id,
				    const struct mpipe_vid_rpc_channel *vid_channel)
{
	__ASSERT_NO_MSG(vtc != NULL);
	__ASSERT_NO_MSG(vid_channel != NULL && vid_channel->channel != NULL &&
			vid_channel->format != NULL);

	struct mpipe_element *self = &vtc->transform.transform.element;
	struct mpipe_transform *transform = &vtc->transform.transform;
	int ret;

	vtc->transform.channel = vid_channel->channel;
	vtc->format = vid_channel->format;

	ret = mpipe_transform_client_init(&vtc->transform, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_transform_client");

	transform->in_pool = &vtc->in_pool.pool;
	transform->out_pool = &vtc->out_pool.pool;

	transform->sink_pad.enum_caps_fn = mpipe_vid_transform_client_enum_caps;
	transform->src_pad.enum_caps_fn = mpipe_vid_transform_client_enum_caps;

	transform->set_caps = mpipe_vid_transform_client_set_caps;
	transform->transform_caps = mpipe_vid_transform_client_transform_caps;
	/* Initialize buffer pools */
	mpipe_vid_buffer_pool_client_init(transform->in_pool);
	mpipe_vid_buffer_pool_client_init(transform->out_pool);

	/*
	 * The pool parameters are asked for once. The supported formats are not:
	 * the pads enumerate them from the remote on demand, so the element keeps
	 * no list of its own and its pad caps stay ANY until one is negotiated.
	 */
	(void)mpipe_vid_transform_client_probe_pool(vtc, MPIPE_PAD_SINK, transform->in_pool);
	(void)mpipe_vid_transform_client_probe_pool(vtc, MPIPE_PAD_SRC, transform->out_pool);

	return 0;
}
