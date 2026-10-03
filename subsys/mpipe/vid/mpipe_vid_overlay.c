/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>

LOG_MODULE_REGISTER(mpipe_vid_overlay, CONFIG_MPIPE_LOG_LEVEL);

/* Formats the element can hand to a draw callback */
static const uint32_t vid_overlay_pixfmts[] = {
	VIDEO_PIX_FMT_RGB565,
	VIDEO_PIX_FMT_BGRX32,
};

static bool vid_overlay_supports(uint32_t pixfmt)
{
	for (size_t i = 0; i < ARRAY_SIZE(vid_overlay_pixfmts); i++) {
		if (vid_overlay_pixfmts[i] == pixfmt) {
			return true;
		}
	}

	return false;
}

static int vid_overlay_candidate(uint32_t pixfmt, struct mpipe_structure *out)
{
	struct mpipe_value v;
	int ret;

	ret = mpipe_structure_init(out, MPIPE_MEDIA_VIDEO);
	if (ret != 0) {
		return ret;
	}

	v.type = MPIPE_TYPE_UINT;
	v.v_uint = pixfmt;
	ret = mpipe_structure_append_value(out, MPIPE_CAPS_PIXEL_FORMAT, &v);
	if (ret != 0) {
		mpipe_structure_clear(out);
	}

	return ret;
}

static int vid_overlay_enum_caps(struct mpipe_pad *pad, uint32_t index,
				 const struct mpipe_structure *filter, struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	int ret;

	ARG_UNUSED(pad);

	if (index >= ARRAY_SIZE(vid_overlay_pixfmts)) {
		return -ENOENT;
	}

	ret = vid_overlay_candidate(vid_overlay_pixfmts[index], &candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int vid_overlay_transform_caps(struct mpipe_transform *self,
				      enum mpipe_pad_direction direction,
				      const struct mpipe_structure *in, uint32_t index,
				      struct mpipe_structure *out)
{
	int ret;

	ARG_UNUSED(self);
	ARG_UNUSED(direction);

	if (in == NULL) {
		return -EINVAL;
	}

	if (mpipe_structure_is_any(in)) {
		/* Constrains nothing: every supported format is reachable */
		if (index >= ARRAY_SIZE(vid_overlay_pixfmts)) {
			return -ENOENT;
		}

		return vid_overlay_candidate(vid_overlay_pixfmts[index], out);
	}

	/* Drawing preserves the format: the identity is the only transformation */
	if (index > 0U) {
		return -ENOENT;
	}

	const struct mpipe_value *pix = mpipe_structure_get_value(in, MPIPE_CAPS_PIXEL_FORMAT);

	if (pix == NULL || pix->type != MPIPE_TYPE_UINT ||
	    !vid_overlay_supports(mpipe_value_get_uint(pix))) {
		return -ENOENT;
	}

	ret = vid_overlay_candidate(mpipe_value_get_uint(pix), out);
	if (ret != 0) {
		return ret;
	}

	/* Drawing changes nothing else; an input that does not constrain a field is fine */
	static const uint8_t passthrough[] = {MPIPE_CAPS_IMAGE_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT,
					      MPIPE_CAPS_FRAME_INTERVAL};

	for (uint8_t i = 0; i < ARRAY_SIZE(passthrough); i++) {
		ret = mpipe_structure_copy_field(in, out, passthrough[i]);
		if (ret != 0 && ret != -ENOENT) {
			mpipe_structure_clear(out);
			return ret;
		}
	}

	return 0;
}

static int vid_overlay_set_caps(struct mpipe_transform *transform,
				enum mpipe_pad_direction direction,
				const struct mpipe_structure *caps)
{
	struct mpipe_vid_overlay *overlay = (struct mpipe_vid_overlay *)transform;
	const struct mpipe_value *v;
	int ret;

	if (caps == NULL) {
		return -EINVAL;
	}

	ret = mpipe_transform_set_caps(transform, direction, caps);
	if (ret < 0) {
		return ret;
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_PIXEL_FORMAT);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		overlay->pixfmt = mpipe_value_get_uint(v);
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_WIDTH);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		overlay->width = (uint16_t)mpipe_value_get_uint(v);
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_HEIGHT);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		overlay->height = (uint16_t)mpipe_value_get_uint(v);
	}

	if (overlay->width != 0U && overlay->height != 0U && overlay->pixfmt != 0U) {
		overlay->out_pool.config.size = (uint32_t)overlay->width * overlay->height *
						video_bits_per_pixel(overlay->pixfmt) /
						BITS_PER_BYTE;
	}

	return 0;
}

static int vid_overlay_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
				  struct net_buf **out_buf)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)pad->object.container;
	struct mpipe_vid_overlay *overlay = (struct mpipe_vid_overlay *)transform;
	struct mpipe_buffer_pool *out_pool = transform->out_pool;
	struct net_buf *cur;
	struct net_buf *next;

	if (overlay->width == 0U || overlay->height == 0U || overlay->pixfmt == 0U) {
		LOG_ERR("Missing negotiated caps");
		net_buf_unref(in_buf);
		return -EINVAL;
	}

	*out_buf = NULL;
	cur = in_buf;
	while (cur != NULL) {
		struct net_buf *out = NULL;
		struct mpipe_buffer_meta *meta;
		uint32_t len =
			MIN(overlay->out_pool.config.size, mpipe_buffer_get_meta(cur)->bytes_used);

		/*
		 * A frame nobody else holds is drawn in place: only behind a tee,
		 * where every branch sees the same payload, is the copy needed. A
		 * view shares its parent's bytes with other views, so it is copied
		 * as well.
		 */
		if (cur->ref == 1U && mpipe_buffer_get_meta(cur)->parent == NULL) {
			if (overlay->cb.fn != NULL) {
				overlay->cb.fn(cur->data, overlay->width, overlay->height,
					       overlay->pixfmt, overlay->cb.user_data);
			}
			next = cur->frags;
			cur->frags = NULL;
			if (*out_buf == NULL) {
				*out_buf = cur;
			} else {
				net_buf_frag_add(*out_buf, cur);
			}
			cur = next;
			continue;
		}

		if (out_pool->acquire_buffer(out_pool, &out) != 0 || out == NULL) {
			LOG_ERR("Failed to acquire output buffer");
			goto err;
		}

		memcpy(out->data, cur->data, len);

		if (overlay->cb.fn != NULL) {
			overlay->cb.fn(out->data, overlay->width, overlay->height, overlay->pixfmt,
				       overlay->cb.user_data);
		}

		meta = mpipe_buffer_get_meta(out);
		meta->bytes_used = len;
		meta->timestamp = mpipe_buffer_get_meta(cur)->timestamp;
		out->len = len;

		if (meta->driver_buf != NULL) {
			((struct video_buffer *)meta->driver_buf)->bytesused = len;
		}

		if (*out_buf == NULL) {
			*out_buf = out;
		} else {
			net_buf_frag_add(*out_buf, out);
		}

		next = cur->frags;
		cur->frags = NULL;
		net_buf_unref(cur);
		cur = next;
	}

	return 0;
err:
	net_buf_unref(cur);
	if (*out_buf != NULL) {
		net_buf_unref(*out_buf);
		*out_buf = NULL;
	}

	return -EIO;
}

static int mpipe_vid_overlay_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_vid_overlay *overlay = (struct mpipe_vid_overlay *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_VID_OVERLAY_DRAW_CB:
		overlay->cb = *(const struct mpipe_vid_overlay_cb *)val;
		return 0;
	default:
		LOG_ERR("Property %d is unknown", key);
		return -ENOTSUP;
	}
}

/* Frame-sized output buffers come from the video buffer heap */
static int vid_overlay_pool_start(struct mpipe_buffer_pool *pool)
{
	struct mpipe_vid_overlay *overlay = CONTAINER_OF(pool, struct mpipe_vid_overlay, out_pool);

	if (pool->config.min_buffers == 0U) {
		pool->config.min_buffers = 1U;
	}

	if (pool->config.min_buffers > CONFIG_VIDEO_BUFFER_POOL_NUM_MAX) {
		LOG_ERR("min_buffers=%u exceeds CONFIG_VIDEO_BUFFER_POOL_NUM_MAX=%u",
			pool->config.min_buffers, CONFIG_VIDEO_BUFFER_POOL_NUM_MAX);
		return -EINVAL;
	}

	overlay->vbuf_count = (uint8_t)pool->config.min_buffers;

	for (uint8_t i = 0; i < overlay->vbuf_count; i++) {
		struct video_buffer *vbuf = video_buffer_alloc(pool->config.size, K_NO_WAIT);

		if (vbuf == NULL) {
			LOG_ERR("Failed to allocate video buffer %u", i);
			return -ENOBUFS;
		}
		overlay->vbufs[i] = vbuf;
		k_fifo_put(&overlay->free_fifo, vbuf);
	}

	return 0;
}

static int vid_overlay_pool_stop(struct mpipe_buffer_pool *pool)
{
	struct mpipe_vid_overlay *overlay = CONTAINER_OF(pool, struct mpipe_vid_overlay, out_pool);

	for (uint8_t i = 0; i < overlay->vbuf_count; i++) {
		if (overlay->vbufs[i] != NULL) {
			(void)video_buffer_release(overlay->vbufs[i]);
			overlay->vbufs[i] = NULL;
		}
	}

	overlay->vbuf_count = 0;

	return 0;
}

static int vid_overlay_pool_acquire(struct mpipe_buffer_pool *pool, struct net_buf **out)
{
	struct mpipe_vid_overlay *overlay = CONTAINER_OF(pool, struct mpipe_vid_overlay, out_pool);
	struct video_buffer *vbuf;
	struct mpipe_buffer_meta *meta;

	__ASSERT_NO_MSG(pool != NULL);
	__ASSERT_NO_MSG(out != NULL);

	vbuf = k_fifo_get(&overlay->free_fifo, K_FOREVER);
	if (vbuf == NULL) {
		return -ENOBUFS;
	}

	*out = net_buf_alloc_with_data(pool->nb_pool, vbuf->buffer, vbuf->size, K_NO_WAIT);
	if (*out == NULL) {
		k_fifo_put(&overlay->free_fifo, vbuf);
		return -ENOBUFS;
	}

	meta = mpipe_buffer_get_meta(*out);
	meta->pool = pool;
	meta->driver_buf = vbuf;
	meta->priv = NULL;
	meta->bytes_used = 0;
	meta->timestamp = 0;
	(*out)->len = 0;

	return 0;
}

static int vid_overlay_pool_release(struct mpipe_buffer_pool *pool, struct net_buf *buf)
{
	struct mpipe_vid_overlay *overlay = CONTAINER_OF(pool, struct mpipe_vid_overlay, out_pool);
	struct video_buffer *vbuf;

	__ASSERT_NO_MSG(pool != NULL);
	__ASSERT_NO_MSG(buf != NULL);

	vbuf = (struct video_buffer *)mpipe_buffer_get_meta(buf)->driver_buf;
	if (vbuf != NULL) {
		k_fifo_put(&overlay->free_fifo, vbuf);
	}

	return 0;
}

int mpipe_vid_overlay_init(struct mpipe_vid_overlay *overlay, uint8_t id)
{
	__ASSERT_NO_MSG(overlay != NULL);

	struct mpipe_element *self = &overlay->transform.element;
	struct mpipe_transform *transform = &overlay->transform;
	int ret = mpipe_transform_init(transform, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_overlay");

	self->object.set_property = mpipe_vid_overlay_set_property;

	transform->sink_pad.enum_caps_fn = vid_overlay_enum_caps;
	transform->src_pad.enum_caps_fn = vid_overlay_enum_caps;

	overlay->width = 0;
	overlay->height = 0;
	overlay->pixfmt = 0;
	overlay->cb.fn = NULL;
	overlay->cb.user_data = NULL;

	k_fifo_init(&overlay->free_fifo);
	memset(overlay->vbufs, 0, sizeof(overlay->vbufs));
	overlay->vbuf_count = 0;

	mpipe_buffer_pool_init(&overlay->out_pool);
	overlay->out_pool.start = vid_overlay_pool_start;
	overlay->out_pool.stop = vid_overlay_pool_stop;
	overlay->out_pool.acquire_buffer = vid_overlay_pool_acquire;
	overlay->out_pool.release_buffer = vid_overlay_pool_release;

	const struct mpipe_buffer_pool_config pool_req = {
		.size = 0,
		.align = 1,
		.min_buffers = 1,
		.max_buffers = CONFIG_VIDEO_BUFFER_POOL_NUM_MAX,
	};

	(void)mpipe_buffer_pool_set_req_config(&overlay->out_pool, &pool_req);

	transform->mode = MPIPE_TRANSFORM_MODE_NORMAL;
	transform->out_pool = &overlay->out_pool;
	transform->set_caps = vid_overlay_set_caps;
	transform->transform_caps = vid_overlay_transform_caps;
	transform->propose_buffer_pool = NULL;
	transform->decide_buffer_pool = NULL;
	transform->sink_pad.process_fn = vid_overlay_process_fn;

	return 0;
}
