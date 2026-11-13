/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/formats.h>

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>
#include <zephyr/mpipe/vid/mpipe_vid_object.h>
#include <zephyr/mpipe/vid/mpipe_vid_pipeline_server.h>

LOG_MODULE_REGISTER(mpipe_vid_pipeline_server, CONFIG_MPIPE_LOG_LEVEL);

/* A dimension left out of a capability: every size the wire type can carry */
#define VID_SERVER_DIM_MIN  1U
#define VID_SERVER_DIM_MAX  UINT16_MAX
#define VID_SERVER_DIM_STEP 1U

/*
 * The served elements may leave a dimension open, which the format capability
 * of the video API cannot: say it as a range.
 */
static int vid_server_caps_to_vfc(const struct mpipe_structure *caps, struct video_format_cap *vfc)
{
	static const uint8_t dims[] = {MPIPE_CAPS_IMAGE_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT};
	const struct mpipe_value range =
		MPIPE_VALUE_UINT_RANGE(VID_SERVER_DIM_MIN, VID_SERVER_DIM_MAX, VID_SERVER_DIM_STEP);
	struct mpipe_structure full = *caps;
	int ret;

	if (mpipe_structure_is_any(caps) ||
	    mpipe_structure_get_value(caps, MPIPE_CAPS_PIXEL_FORMAT) == NULL) {
		return -ENOTSUP;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(dims); i++) {
		if (mpipe_structure_get_value(&full, dims[i]) != NULL) {
			continue;
		}

		ret = mpipe_structure_append_value(&full, dims[i], &range);
		if (ret != 0) {
			return ret;
		}
	}

	return mpipe_vid_caps_to_vfc(&full, vfc);
}

int mpipe_vid_pipeline_server_get_buf_caps(struct mpipe_pipeline_server *server,
					   enum mpipe_pad_direction direction, uint8_t *min_buffers,
					   uint16_t *buf_align)
{
	struct mpipe_buffer_pool_config cfg;
	int ret;

	ret = mpipe_pipeline_server_get_pool_config(server, direction, &cfg);
	if (ret != 0) {
		return ret;
	}

	*min_buffers = cfg.min_buffers;
	*buf_align = cfg.align;

	return 0;
}

int mpipe_vid_pipeline_server_get_format_caps(struct mpipe_pipeline_server *server,
					      enum mpipe_pad_direction direction, uint8_t ind,
					      struct video_format_cap *vfc)
{
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_server_enum_caps(server, direction, ind, &caps);
	if (ret != 0) {
		return ret;
	}

	ret = vid_server_caps_to_vfc(&caps, vfc);
	if (ret == -ENOTSUP) {
		LOG_ERR("The served pipeline states no pixel format: add a caps filter");
	}

	return ret;
}

int mpipe_vid_pipeline_server_set_format(struct mpipe_pipeline_server *server,
					 struct video_format *fmt)
{
	struct mpipe_buffer_pool_config cfg;
	enum mpipe_pad_direction direction;
	struct mpipe_structure caps;
	uint32_t bpp;
	int ret;

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		direction = MPIPE_PAD_SINK;
	} else if (fmt->type == VIDEO_BUF_TYPE_OUTPUT) {
		direction = MPIPE_PAD_SRC;
	} else {
		return -EINVAL;
	}

	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					  MPIPE_TYPE_UINT, fmt->pixelformat, MPIPE_CAPS_IMAGE_WIDTH,
					  MPIPE_TYPE_UINT, fmt->width, MPIPE_CAPS_IMAGE_HEIGHT,
					  MPIPE_TYPE_UINT, fmt->height, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_pipeline_server_set_caps(server, direction, &caps);
	if (ret != 0) {
		return ret;
	}

	(void)mpipe_pipeline_server_get_pool_config(server, direction, &cfg);

	bpp = video_bits_per_pixel(fmt->pixelformat);
	if (bpp != 0U) {
		fmt->pitch = fmt->width * bpp / BITS_PER_BYTE;
		fmt->size = fmt->pitch * fmt->height;
	} else {
		/* Compressed: one byte per pixel bounds a frame */
		fmt->pitch = 0;
		fmt->size = fmt->width * fmt->height;
	}

	/* Whole cache lines, so that maintaining a buffer touches no neighbor */
	fmt->size = ROUND_UP(fmt->size, cfg.align);

	return 0;
}

int mpipe_vid_pipeline_server_transform_cap(struct mpipe_pipeline_server *server,
					    enum mpipe_pad_direction direction, uint16_t ind,
					    const struct video_format_cap *vfc,
					    struct video_format_cap *other_vfc)
{
	struct mpipe_structure in;
	struct mpipe_structure out;
	int ret;

	ret = mpipe_vid_vfc_to_caps(vfc, &in);
	if (ret != 0) {
		return ret;
	}

	ret = mpipe_pipeline_server_transform_caps(server, direction, ind, &in, &out);
	if (ret != 0) {
		return ret;
	}

	return vid_server_caps_to_vfc(&out, other_vfc);
}
