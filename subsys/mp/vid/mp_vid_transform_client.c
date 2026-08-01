/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>
#include <zephyr/mp/vid/mp_vid_buffer_pool_client.h>
#include <zephyr/mp/vid/mp_vid_object.h>
#include <zephyr/mp/vid/mp_vid_transform_client.h>

LOG_MODULE_REGISTER(mp_vid_transform_client, CONFIG_MP_LOG_LEVEL);

/* Ask the remote for the pool parameters of one direction, once at init */
static int mp_vid_transform_client_probe_pool(struct mp_vid_transform_client *vtc,
					      enum mp_pad_direction direction,
					      struct mp_buffer_pool *pool)
{
	if (vtc->get_buf_caps_rpc(direction, &pool->config.min_buffers, &pool->config.align) != 0) {
		LOG_ERR("Unable to retrieve buffer pool capabilities");
		return -ENODEV;
	}

	return 0;
}

static int mp_vid_transform_client_enum_caps(struct mp_pad *pad, uint32_t index,
					     const struct mp_structure *filter,
					     struct mp_structure *out)
{
	struct mp_vid_transform_client *vtc =
		(struct mp_vid_transform_client *)pad->object.container;
	struct mp_structure candidate;
	struct video_format_cap vfc;
	int ret;

	if (pad->direction != MP_PAD_SINK && pad->direction != MP_PAD_SRC) {
		return -EINVAL;
	}

	if (vtc->get_format_caps_rpc(pad->direction, (uint8_t)index, &vfc) != 0) {
		return -ENOENT;
	}

	ret = mp_vfc_to_structure(&vfc, &candidate);
	if (ret != 0) {
		return ret;
	}

	if (filter == NULL) {
		*out = candidate;
		return 0;
	}

	ret = mp_structure_intersect(&candidate, filter, out);
	mp_structure_clear(&candidate);

	return (ret != 0) ? -EAGAIN : 0;
}

static int mp_vid_transform_client_set_caps(struct mp_transform *transform,
					    enum mp_pad_direction direction,
					    const struct mp_structure *caps)
{
	struct mp_vid_transform_client *vtc = (struct mp_vid_transform_client *)transform;
	struct mp_buffer_pool *pool = NULL;
	struct video_format_cap vfc = {0};
	struct video_format fmt;
	int ret;

	if (caps == NULL || !mp_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	if (direction == MP_PAD_SINK) {
		fmt.type = VIDEO_BUF_TYPE_INPUT;
		pool = transform->inpool;
	} else if (direction == MP_PAD_SRC) {
		fmt.type = VIDEO_BUF_TYPE_OUTPUT;
		pool = transform->outpool;
	} else {
		LOG_ERR("Pad direction is invalid");
		return -EINVAL;
	}

	ret = mp_structure_to_vfc(caps, &vfc);
	if (ret < 0) {
		return ret;
	}

	fmt.pixelformat = vfc.pixelformat;
	fmt.width = vfc.width_min;
	fmt.height = vfc.height_min;

	ret = vtc->set_format_rpc(&fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format: type=%u pixfmt=%u w=%u h=%u", fmt.type,
			fmt.pixelformat, fmt.width, fmt.height);
		return ret;
	}

	/* Set buffer pool size */
	pool->config.size = fmt.size;

	/* Set pad's caps only when everything is OK */
	return mp_pad_set_caps(direction == MP_PAD_SRC ? &transform->srcpad : &transform->sinkpad,
			       caps);
}

static int mp_vid_transform_client_transform_caps(struct mp_transform *self,
						  enum mp_pad_direction direction,
						  const struct mp_structure *in, uint32_t index,
						  struct mp_structure *out)
{
	struct mp_vid_transform_client *vtc = (struct mp_vid_transform_client *)self;
	struct video_format_cap vfc, other_vfc;

	if (direction != MP_PAD_SINK && direction != MP_PAD_SRC) {
		return -EINVAL;
	}

	if (in == NULL || mp_structure_to_vfc(in, &vfc) < 0) {
		return -ENOENT;
	}

	if (vtc->transform_cap_rpc(direction, (uint16_t)index, &vfc, &other_vfc) != 0) {
		return -ENOENT;
	}

	return mp_vfc_to_structure(&other_vfc, out);
}

void mp_vid_transform_client_init(struct mp_element *self)
{
	struct mp_transform *transform = (struct mp_transform *)self;
	struct mp_vid_transform_client *vtc = (struct mp_vid_transform_client *)transform;

	/* Init base class */
	mp_transform_client_init(self);

	transform->inpool = &vtc->inpool.pool;
	transform->outpool = &vtc->outpool.pool;

	/*
	 * The pool parameters are asked for once. The supported formats are not:
	 * the pads enumerate them from the remote on demand, so the element keeps
	 * no list of its own and its pad caps stay ANY until one is negotiated.
	 */
	(void)mp_vid_transform_client_probe_pool(vtc, MP_PAD_SINK, transform->inpool);
	(void)mp_vid_transform_client_probe_pool(vtc, MP_PAD_SRC, transform->outpool);

	transform->sinkpad.enum_capsfn = mp_vid_transform_client_enum_caps;
	transform->srcpad.enum_capsfn = mp_vid_transform_client_enum_caps;

	transform->set_caps = mp_vid_transform_client_set_caps;
	transform->transform_caps = mp_vid_transform_client_transform_caps;
	/* Initialize buffer pools */
	mp_vid_buffer_pool_client_init(transform->inpool);
	mp_vid_buffer_pool_client_init(transform->outpool);
}
