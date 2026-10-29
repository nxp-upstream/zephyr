/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/mpipe/mpipe_transform.h"
#include <errno.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>

LOG_MODULE_REGISTER(mpipe_vid_overlay, CONFIG_MPIPE_LOG_LEVEL);

/* Formats the element draws into; each is offered on its own index */
static const uint32_t overlay_pixfmts[] = {
	VIDEO_PIX_FMT_RGB565, VIDEO_PIX_FMT_RGB565X, VIDEO_PIX_FMT_BGR24,
	VIDEO_PIX_FMT_RGB24,  VIDEO_PIX_FMT_XRGB32,  VIDEO_PIX_FMT_ARGB32,
};

static int overlay_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_vid_overlay *self = (struct mpipe_vid_overlay *)obj;
	const struct mpipe_vid_overlay_rects *set = val;
	k_spinlock_key_t k;
	uint8_t count;

	if (key != MPIPE_PROP_VID_OVERLAY_RECTS) {
		return -ENOTSUP;
	}

	if (set == NULL || (set->count > 0U && set->rects == NULL)) {
		return -EINVAL;
	}

	count = MIN(set->count, (uint8_t)CONFIG_MPIPE_VID_OVERLAY_MAX_RECTS);

	k = k_spin_lock(&self->lock);
	memcpy(self->rects, set->rects, count * sizeof(self->rects[0]));
	self->count = count;
	k_spin_unlock(&self->lock, k);

	return 0;
}

static int overlay_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_vid_overlay *self = (struct mpipe_vid_overlay *)obj;
	struct mpipe_vid_overlay_rects *set = val;

	if (key != MPIPE_PROP_VID_OVERLAY_RECTS) {
		return -ENOTSUP;
	}

	if (set == NULL) {
		return -EINVAL;
	}

	set->rects = self->rects;
	set->count = self->count;

	return 0;
}

/* Bytes per pixel of a supported format, 0 for any other */
static uint8_t overlay_bpp(uint32_t pixfmt)
{
	switch (pixfmt) {
	case VIDEO_PIX_FMT_RGB565:
	case VIDEO_PIX_FMT_RGB565X:
		return 2U;
	case VIDEO_PIX_FMT_BGR24:
	case VIDEO_PIX_FMT_RGB24:
		return 3U;
	case VIDEO_PIX_FMT_XRGB32:
	case VIDEO_PIX_FMT_ARGB32:
		return 4U;
	default:
		return 0U;
	}
}

/* The color in the frame's own byte layout, written once per pixel */
static void overlay_pack(uint32_t pixfmt, uint32_t rgb, uint8_t px[4])
{
	uint8_t r = (uint8_t)(rgb >> 16);
	uint8_t g = (uint8_t)(rgb >> 8);
	uint8_t b = (uint8_t)rgb;
	uint16_t v565 = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));

	switch (pixfmt) {
	case VIDEO_PIX_FMT_RGB565:
		px[0] = (uint8_t)v565;
		px[1] = (uint8_t)(v565 >> 8);
		break;
	case VIDEO_PIX_FMT_RGB565X:
		px[0] = (uint8_t)(v565 >> 8);
		px[1] = (uint8_t)v565;
		break;
	case VIDEO_PIX_FMT_BGR24:
		px[0] = b;
		px[1] = g;
		px[2] = r;
		break;
	case VIDEO_PIX_FMT_RGB24:
		px[0] = r;
		px[1] = g;
		px[2] = b;
		break;
	default:
		/* XRGB32 and ARGB32, stored little endian as B G R A */
		px[0] = b;
		px[1] = g;
		px[2] = r;
		px[3] = 0xFFU;
		break;
	}
}

/* Fill the inclusive area [x0, x1] x [y0, y1], already clipped to the frame */
static void overlay_fill(uint8_t *data, size_t pitch, uint8_t bpp, const uint8_t px[4], uint16_t x0,
			 uint16_t y0, uint16_t x1, uint16_t y1)
{
	for (uint32_t y = y0; y <= y1; y++) {
		uint8_t *p = data + (size_t)y * pitch + (size_t)x0 * bpp;

		for (uint32_t x = x0; x <= x1; x++) {
			memcpy(p, px, bpp);
			p += bpp;
		}
	}
}

static void overlay_draw_rect(const struct mpipe_vid_overlay *self, uint8_t *data,
			      const struct mpipe_vid_overlay_rect *r)
{
	uint8_t bpp = overlay_bpp(self->pixfmt);
	size_t pitch = (size_t)self->width * bpp;
	uint16_t left = r->left;
	uint16_t top = r->top;
	uint16_t right = MIN(r->right, (uint16_t)(self->width - 1U));
	uint16_t bottom = MIN(r->bottom, (uint16_t)(self->height - 1U));
	uint16_t lw = MAX(r->line_width, 1U);
	uint8_t px[4];

	if (left > right || top > bottom) {
		return;
	}

	overlay_pack(self->pixfmt, r->color, px);

	/* Top, bottom, left and right edges, each lw pixels thick inwards */
	overlay_fill(data, pitch, bpp, px, left, top, right, MIN(top + lw - 1U, bottom));
	overlay_fill(data, pitch, bpp, px, left, (bottom >= top + lw) ? bottom - lw + 1U : top,
		     right, bottom);
	overlay_fill(data, pitch, bpp, px, left, top, MIN(left + lw - 1U, right), bottom);
	overlay_fill(data, pitch, bpp, px, (right >= left + lw) ? right - lw + 1U : left, top,
		     right, bottom);
}

static int overlay_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
			      struct net_buf **out_buf)
{
	struct mpipe_vid_overlay *self = CONTAINER_OF(
		pad->object.container, struct mpipe_vid_overlay, transform.element.object);
	struct mpipe_vid_overlay_rect rects[CONFIG_MPIPE_VID_OVERLAY_MAX_RECTS];
	uint8_t bpp = overlay_bpp(self->pixfmt);
	size_t need = (size_t)self->width * self->height * bpp;
	k_spinlock_key_t k;
	uint8_t count;

	*out_buf = in_buf;

	if (in_buf == NULL || in_buf->data == NULL) {
		return -EINVAL;
	}

	/* A strip or a short buffer is forwarded without drawing */
	if (bpp == 0U || mpipe_buffer_get_meta(in_buf)->bytes_used < need) {
		return 0;
	}

	k = k_spin_lock(&self->lock);
	count = self->count;
	memcpy(rects, self->rects, count * sizeof(rects[0]));
	k_spin_unlock(&self->lock, k);

	for (uint8_t i = 0; i < count; i++) {
		overlay_draw_rect(self, in_buf->data, &rects[i]);
	}

	return 0;
}

/*
 * Only the pixel format is constrained; width, height and frame interval pass
 * through and are advertised as open ranges so the caps share fields with the
 * neighbors and negotiation can intersect them.
 */
static int overlay_enum_caps(struct mpipe_pad *pad, uint32_t index,
			     const struct mpipe_structure *filter, struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	int ret;

	ARG_UNUSED(pad);

	if (index >= ARRAY_SIZE(overlay_pixfmts)) {
		return -ENOENT;
	}

	ret = mpipe_structure_init_fields(
		&candidate, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		overlay_pixfmts[index], MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT_RANGE, 1U,
		UINT16_MAX, 1U, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT_RANGE, 1U, UINT16_MAX, 1U,
		MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int overlay_set_caps(struct mpipe_transform *transform, enum mpipe_pad_direction direction,
			    const struct mpipe_structure *caps)
{
	struct mpipe_vid_overlay *self = (struct mpipe_vid_overlay *)transform;
	const struct mpipe_value *v;

	if (caps == NULL) {
		return -EINVAL;
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_PIXEL_FORMAT);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		self->pixfmt = mpipe_value_get_uint(v);
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_WIDTH);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		self->width = (uint16_t)mpipe_value_get_uint(v);
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_HEIGHT);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		self->height = (uint16_t)mpipe_value_get_uint(v);
	}

	if (overlay_bpp(self->pixfmt) == 0U) {
		LOG_ERR("Unsupported pixel format 0x%08x", self->pixfmt);
		return -ENOTSUP;
	}

	return mpipe_transform_set_caps(transform, direction, caps);
}

/* In place: downstream's requirement is relayed to upstream unchanged */
static int overlay_decide_buffer_pool(struct mpipe_transform *transform,
				      struct mpipe_dispatch *query)
{
	struct mpipe_vid_overlay *self = (struct mpipe_vid_overlay *)transform;

	self->alloc_cfg = (query->pool != NULL) ? query->pool->config : query->pool_cfg;

	return 0;
}

static int overlay_propose_buffer_pool(struct mpipe_transform *transform,
				       struct mpipe_dispatch *query)
{
	struct mpipe_vid_overlay *self = (struct mpipe_vid_overlay *)transform;

	query->pool_cfg = self->alloc_cfg;

	return 0;
}

int mpipe_vid_overlay_init(struct mpipe_vid_overlay *overlay, uint8_t id)
{
	struct mpipe_transform *transform;
	int ret;

	if (overlay == NULL) {
		return -EINVAL;
	}

	transform = &overlay->transform;
	ret = mpipe_transform_init(transform, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(&transform->element, "vid_overlay");

	overlay->width = 0U;
	overlay->height = 0U;
	overlay->pixfmt = 0U;
	overlay->count = 0U;
	memset(&overlay->alloc_cfg, 0, sizeof(overlay->alloc_cfg));

	transform->element.object.set_property = overlay_set_property;
	transform->element.object.get_property = overlay_get_property;

	transform->mode = MPIPE_TRANSFORM_MODE_INPLACE;
	transform->sink_pad.process_fn = overlay_process_fn;
	transform->sink_pad.enum_caps_fn = overlay_enum_caps;
	transform->src_pad.enum_caps_fn = overlay_enum_caps;
	transform->set_caps = overlay_set_caps;
	transform->decide_buffer_pool = overlay_decide_buffer_pool;
	transform->propose_buffer_pool = overlay_propose_buffer_pool;

	return 0;
}
