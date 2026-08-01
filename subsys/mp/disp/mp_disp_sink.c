/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/disp/mp_disp_sink.h>

LOG_MODULE_REGISTER(mp_disp_sink, CONFIG_MP_LOG_LEVEL);

#define DEFAULT_PROP_DEVICE DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_display))

/* Default supported minimum width/height may depend on the HW but currently no way to get it */
#define DEFAULT_WIDTH_MIN  1
#define DEFAULT_HEIGHT_MIN 1

struct mp_disp_vid_pix_fmt {
	enum display_pixel_format disp_fmt;
	uint32_t vid_fmt;
};

/*
 * Pixel format mapping between video and display
 * Note: video fourcc pixel format is the standard used for caps
 */
static const struct mp_disp_vid_pix_fmt mp_disp_vid_pix_fmt_map[] = {
	{PIXEL_FORMAT_RGB_565, VIDEO_PIX_FMT_RGB565},
	{PIXEL_FORMAT_RGB_565X, VIDEO_PIX_FMT_RGB565X},
	{PIXEL_FORMAT_RGB_888, VIDEO_PIX_FMT_BGR24},
	{PIXEL_FORMAT_ARGB_8888, VIDEO_PIX_FMT_BGRA32},
	{PIXEL_FORMAT_XRGB_8888, VIDEO_PIX_FMT_BGRX32},
	{PIXEL_FORMAT_L_8, VIDEO_PIX_FMT_GREY},
};

static uint32_t disp_to_vid_pix_fmt(enum display_pixel_format disp_fmt)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(mp_disp_vid_pix_fmt_map); i++) {
		if (mp_disp_vid_pix_fmt_map[i].disp_fmt == disp_fmt) {
			return mp_disp_vid_pix_fmt_map[i].vid_fmt;
		}
	}

	return 0;
}

static enum display_pixel_format vid_to_disp_pix_fmt(uint32_t vid_fmt)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(mp_disp_vid_pix_fmt_map); i++) {
		if (mp_disp_vid_pix_fmt_map[i].vid_fmt == vid_fmt) {
			return mp_disp_vid_pix_fmt_map[i].disp_fmt;
		}
	}

	return 0;
}

static int mp_disp_sink_setup(struct mp_disp_sink *disp_sink,
			      const enum display_pixel_format pixfmt)
{
	int ret = 0;

	ret = display_set_pixel_format(disp_sink->display_dev, pixfmt);
	if (ret != 0) {
		LOG_ERR("Unable to set display format");
		return ret;
	}

	/* Turn off blanking if driver supports it */
	ret = display_blanking_off(disp_sink->display_dev);
	if (ret == -ENOSYS) {
		LOG_WRN("Display blanking off not available");
		ret = 0;
	}

	return ret;
}

/*
 * The display advertises the formats it accepts as a bitmask, so the caps of
 * the n-th supported format can be produced directly from it and none of the
 * others has to exist.
 */
static int mp_disp_sink_enum_caps(struct mp_pad *pad, uint32_t index,
				  const struct mp_structure *filter, struct mp_structure *out)
{
	struct mp_disp_sink *disp = (struct mp_disp_sink *)pad->object.container;
	struct display_capabilities display_caps;
	struct mp_structure candidate;
	uint32_t vid_fmt = 0;
	uint32_t matched = 0;
	int ret;

	display_get_capabilities(disp->display_dev, &display_caps);

	for (uint8_t i = 0; i < ARRAY_SIZE(mp_disp_vid_pix_fmt_map); i++) {
		/* Use video formats as reference formats for caps */
		uint32_t fmt = disp_to_vid_pix_fmt(display_caps.supported_pixel_formats &
						   mp_disp_vid_pix_fmt_map[i].disp_fmt);

		if (fmt == 0) {
			continue;
		}

		if (matched == index) {
			vid_fmt = fmt;
			break;
		}

		matched++;
	}

	if (vid_fmt == 0) {
		return -ENOENT;
	}

	ret = mp_structure_init_fields(
		&candidate, MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT, vid_fmt,
		MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT_RANGE, DEFAULT_WIDTH_MIN,
		display_caps.x_resolution, 1, MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT_RANGE,
		DEFAULT_HEIGHT_MIN, display_caps.y_resolution, 1, MP_CAPS_END);
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

static int mp_disp_sink_set_caps(struct mp_sink *sink, const struct mp_structure *caps)
{
	struct mp_disp_sink *disp_sink = (struct mp_disp_sink *)sink;
	const struct mp_value *value = mp_structure_get_value(caps, MP_CAPS_PIXEL_FORMAT);
	enum display_pixel_format disp_fmt;

	if (value == NULL) {
		return -EINVAL;
	}

	disp_fmt = vid_to_disp_pix_fmt(mp_value_get_uint(value));
	if (disp_fmt == 0 || mp_disp_sink_setup(disp_sink, disp_fmt) != 0) {
		return -EINVAL;
	}

	return mp_pad_set_caps(&sink->sinkpad, caps);
}

static int mp_disp_sink_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_disp_sink *disp_sink = (struct mp_disp_sink *)obj;

	switch (key) {
	case MP_PROP_DISP_SINK_DEVICE:
		disp_sink->display_dev = val;

		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mp_disp_sink_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_disp_sink *disp_sink = (struct mp_disp_sink *)obj;

	switch (key) {
	case MP_PROP_DISP_SINK_DEVICE:
		*(const struct device **)val = disp_sink->display_dev;

		return 0;
	default:
		return -ENOTSUP;
	}
}

int mp_disp_sink_chainfn(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mp_disp_sink *disp_sink =
		CONTAINER_OF(pad->object.container, struct mp_disp_sink, sink.element.object);
	const struct mp_value *value = mp_structure_get_value(&pad->caps, MP_CAPS_PIXEL_FORMAT);
	enum display_pixel_format disp_fmt = 0;
	uint32_t bytes_per_line;
	uint32_t width = 0;
	struct net_buf *cur;
	struct net_buf *next;

	if (value != NULL) {
		disp_fmt = vid_to_disp_pix_fmt(mp_value_get_uint(value));
	}

	value = mp_structure_get_value(&pad->caps, MP_CAPS_IMAGE_WIDTH);
	if (value != NULL) {
		width = mp_value_get_uint(value);
	}

	/* Sink returns NULL for output buffer as it is at the end of the chain */
	*out_buf = NULL;

	if (in_buf == NULL) {
		return 0;
	}

	/*
	 * The height of a fragment is derived from its size, the frame width and
	 * the pixel depth. Caps that never got negotiated leave both the format
	 * and the width unset, so report that instead of dividing by zero.
	 */
	bytes_per_line = width * DISPLAY_BITS_PER_PIXEL(disp_fmt) / BITS_PER_BYTE;
	if (bytes_per_line == 0) {
		LOG_ERR("No negotiated pixel format and width to display a buffer with");
		net_buf_unref(in_buf);
		return -ENODATA;
	}

	/* Input may be a fragment chain. Display each fragment then unref it */
	cur = in_buf;
	while (cur != NULL) {
		/*
		 * The buffer is assumed to carry a video buffer.
		 * TODO: Add support for other buffer types
		 */
		struct video_buffer *vbuf =
			(struct video_buffer *)mp_buffer_get_meta(cur)->driver_buf;
		struct display_buffer_descriptor buf_desc = {
			.buf_size = mp_buffer_get_meta(cur)->bytes_used,
		};

		next = cur->frags;
		cur->frags = NULL;

		buf_desc.width = width;
		buf_desc.pitch = buf_desc.width;
		/* Do not get height from caps as sometimes buffer is just a partial frame */
		buf_desc.height = buf_desc.buf_size / bytes_per_line;

		/* line_offset is only used to support partial video frame */
		if (vbuf != NULL) {
			display_write(disp_sink->display_dev, 0, vbuf->line_offset, &buf_desc,
				      vbuf->buffer);
		} else {
			/* Fallback to net_buf data if no video_buffer metadata */
			display_write(disp_sink->display_dev, 0, 0, &buf_desc, cur->data);
		}

		net_buf_unref(cur);
		cur = next;
	}

	return 0;
}

void mp_disp_sink_init(struct mp_element *self)
{
	struct mp_disp_sink *disp_sink = (struct mp_disp_sink *)self;
	struct mp_sink *sink = &disp_sink->sink;

	/* Init base class */
	mp_sink_init(self);

	disp_sink->display_dev = DEFAULT_PROP_DEVICE;

	self->object.get_property = mp_disp_sink_get_property;
	self->object.set_property = mp_disp_sink_set_property;

	sink->sinkpad.chainfn = mp_disp_sink_chainfn;
	sink->sinkpad.enum_capsfn = mp_disp_sink_enum_caps;
	sink->set_caps = mp_disp_sink_set_caps;
}
