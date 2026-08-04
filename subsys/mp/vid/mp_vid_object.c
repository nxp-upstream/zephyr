/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/video/controls.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/minmax.h>
#include <zephyr/sys/util.h>

#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/vid/mp_vid_object.h>
#include <zephyr/mp/vid/mp_vid_property.h>

LOG_MODULE_REGISTER(mp_vid_object, CONFIG_MP_LOG_LEVEL);

static int set_dimension_fields(const struct mp_structure *structure, uint8_t key, uint32_t *min,
				uint32_t *max, uint16_t *step)
{
	const struct mp_value *value = mp_structure_get_value(structure, key);

	if (value == NULL) {
		return -EINVAL;
	}

	if (value->type == MP_TYPE_UINT_RANGE) {
		*min = mp_value_get_uint_range_min(value);
		*max = mp_value_get_uint_range_max(value);
		*step = (uint16_t)mp_value_get_uint_range_step(value);
	} else if (value->type == MP_TYPE_UINT) {
		*min = mp_value_get_uint(value);
		*max = *min;
		*step = 0;
	} else {
		return -EINVAL;
	}

	return 0;
}

int mp_structure_to_vfc(const struct mp_structure *structure, struct video_format_cap *vfc)
{
	int ret;
	const struct mp_value *value;

	/* Get pixel format field */
	value = mp_structure_get_value(structure, MP_CAPS_PIXEL_FORMAT);
	if (value == NULL) {
		return -EINVAL;
	}
	if (value->type != MP_TYPE_UINT) {
		return -EINVAL;
	}

	vfc->pixelformat = mp_value_get_uint(value);

	/* Get width fields */
	ret = set_dimension_fields(structure, MP_CAPS_IMAGE_WIDTH, &vfc->width_min, &vfc->width_max,
				   &vfc->width_step);
	if (ret < 0) {
		return ret;
	}

	/* Get height fields */
	return set_dimension_fields(structure, MP_CAPS_IMAGE_HEIGHT, &vfc->height_min,
				    &vfc->height_max, &vfc->height_step);
}

int mp_structure_to_format(const struct mp_structure *structure, enum video_buf_type type,
			   struct video_format *fmt)
{
	struct video_format_cap vfc = {0};
	int ret;

	if (structure == NULL || fmt == NULL) {
		return -EINVAL;
	}

	ret = mp_structure_to_vfc(structure, &vfc);
	if (ret < 0) {
		return ret;
	}

	fmt->type = type;
	fmt->pixelformat = vfc.pixelformat;
	fmt->width = vfc.width_min;
	fmt->height = vfc.height_min;

	return 0;
}

int mp_vfc_to_structure(const struct video_format_cap *vfc, struct mp_structure *out)
{
	if (vfc == NULL || out == NULL) {
		return -EINVAL;
	}

	return mp_structure_init_fields(out, MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT,
					vfc->pixelformat, MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT_RANGE,
					vfc->width_min, vfc->width_max, vfc->width_step,
					MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT_RANGE, vfc->height_min,
					vfc->height_max, vfc->height_step, MP_CAPS_END);
}

static uint32_t frmival_to_usec(const struct video_frmival *frmival)
{
	if (frmival->denominator == 0) {
		return 0;
	}

	return (uint32_t)DIV_ROUND_CLOSEST((uint64_t)frmival->numerator * USEC_PER_SEC,
					   frmival->denominator);
}

/*
 * Number of capabilities a format entry contributes. A stepwise device covers
 * every interval with one range, so it contributes one; a discrete device
 * contributes one per interval, as alternatives belong on the enumeration
 * index rather than in a list value.
 */
static uint32_t frmival_count(const struct device *vdev, struct video_format *fmt)
{
	struct video_frmival_enum fie = {0};
	uint32_t count = 0;

	fie.format = fmt;
	while (video_enum_frmival(vdev, &fie) == 0) {
		if (fie.type == VIDEO_FRMIVAL_TYPE_STEPWISE) {
			return 1;
		}

		if (fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
			count++;
		}

		fie.index++;
	}

	/* Devices without frame interval support still expose their format */
	return (count > 0) ? count : 1;
}

/* Describe the frame interval at @p fi_index of a format entry */
static void append_frmival_at(const struct device *vdev, struct video_format *fmt,
			      struct mp_structure *caps_item, uint32_t fi_index)
{
	struct video_frmival_enum fie = {0};
	struct mp_value value;
	uint32_t count = 0;

	fie.format = fmt;
	while (video_enum_frmival(vdev, &fie) == 0) {
		if (fie.type == VIDEO_FRMIVAL_TYPE_STEPWISE) {
			value.type = MP_TYPE_UINT_RANGE;
			value.range.min.v_uint = frmival_to_usec(&fie.stepwise.min);
			value.range.max.v_uint = frmival_to_usec(&fie.stepwise.max);
			value.range.step.v_uint = frmival_to_usec(&fie.stepwise.step);
			(void)mp_structure_append_value(caps_item, MP_CAPS_FRAME_INTERVAL, &value);
			return;
		}

		if (fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
			if (count == fi_index) {
				value.type = MP_TYPE_UINT;
				value.v_uint = frmival_to_usec(&fie.discrete);
				(void)mp_structure_append_value(caps_item, MP_CAPS_FRAME_INTERVAL,
								&value);
				return;
			}

			count++;
		}

		fie.index++;
	}

	/* Devices without frame interval support report nothing at all */
}

int mp_vid_object_probe_bounds(struct mp_vid_object *vid_obj)
{
	struct video_caps vcaps;
	struct video_selection sel;
	struct video_rect rect;
	int ret;

	if (vid_obj == NULL || vid_obj->vdev == NULL) {
		return -EINVAL;
	}

	vcaps = (struct video_caps){.type = vid_obj->type};
	sel = (struct video_selection){
		.type = vid_obj->type,
		.target = VIDEO_SEL_TGT_CROP,
	};

	if (video_get_caps(vid_obj->vdev, &vcaps) != 0) {
		LOG_WRN("Unable to retrieve device's capabilities");
		return -ENODEV;
	}

	vid_obj->bounds.vcaps = vcaps;
	vid_obj->bounds.crop_w = UINT32_MAX;
	vid_obj->bounds.crop_h = UINT32_MAX;
	vid_obj->bounds.comp_min_w = UINT32_MAX;
	vid_obj->bounds.comp_min_h = UINT32_MAX;
	vid_obj->bounds.comp_max_w = 0;
	vid_obj->bounds.comp_max_h = 0;

	/* Get crop selection */
	ret = video_get_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		vid_obj->bounds.crop_w = sel.rect.width;
		vid_obj->bounds.crop_h = sel.rect.height;
	}

	/* Get compose selection upper-bound */
	sel.target = VIDEO_SEL_TGT_COMPOSE_BOUND;
	ret = video_get_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		vid_obj->bounds.comp_max_w = sel.rect.width + sel.rect.left;
		vid_obj->bounds.comp_max_h = sel.rect.height + sel.rect.top;
	}

	/* Memorize the current compose selection */
	sel.target = VIDEO_SEL_TGT_COMPOSE;
	ret = video_get_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		rect = sel.rect;
	}

	/* Probe the compose selection lower-bound */
	sel.target = VIDEO_SEL_TGT_COMPOSE;
	sel.rect = (struct video_rect){.top = 0, .left = 0, .width = 1, .height = 1};
	ret = video_set_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		vid_obj->bounds.comp_min_w = sel.rect.width + sel.rect.left;
		vid_obj->bounds.comp_min_h = sel.rect.height + sel.rect.top;
	}

	/* Set back the original compose selection */
	sel.rect = rect;
	video_set_selection(vid_obj->vdev, &sel);

	/* Set buffer pool's min_buffers and alignment */
	vid_obj->pool.pool.config.min_buffers = vcaps.min_vbuf_count;
	vid_obj->pool.pool.config.align = vcaps.buf_align;

	return 0;
}

/* Build the capability describing a single device format entry */
static int mp_vid_object_build_structure(struct mp_vid_object *vid_obj,
					 const struct video_format_cap *fc, uint32_t fi_index,
					 struct mp_structure *out)
{
	struct video_format fmt = {.type = vid_obj->type};
	int ret;

	ret = mp_structure_init_fields(
		out, MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT, fc->pixelformat,
		MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT_RANGE,
		min3(fc->width_min, vid_obj->bounds.crop_w, vid_obj->bounds.comp_min_w),
		max(fc->width_max, vid_obj->bounds.comp_max_w), fc->width_step,
		MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT_RANGE,
		min3(fc->height_min, vid_obj->bounds.crop_h, vid_obj->bounds.comp_min_h),
		max(fc->height_max, vid_obj->bounds.comp_max_h), fc->height_step, MP_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	/* Get frame interval */
	fmt.pixelformat = fc->pixelformat;
	fmt.width = fc->width_min;
	fmt.height = fc->height_min;
	append_frmival_at(vid_obj->vdev, &fmt, out, fi_index);

	return 0;
}

/*
 * Map a flat enumeration index onto a format entry and one of its frame
 * intervals. The device is walked rather than an offset table being kept, so
 * nothing has to be sized in advance; this runs once per negotiation and each
 * step is a driver table lookup.
 */
static int mp_vid_object_locate(struct mp_vid_object *vid_obj, uint32_t index,
				const struct video_format_cap **fc, uint32_t *fi_index)
{
	uint32_t remaining = index;

	if (vid_obj->bounds.vcaps.format_caps == NULL) {
		return -ENOENT;
	}

	for (uint8_t i = 0; vid_obj->bounds.vcaps.format_caps[i].pixelformat != 0; i++) {
		const struct video_format_cap *cap = &vid_obj->bounds.vcaps.format_caps[i];
		struct video_format fmt = {
			.type = vid_obj->type,
			.pixelformat = cap->pixelformat,
			.width = cap->width_min,
			.height = cap->height_min,
		};
		uint32_t count = frmival_count(vid_obj->vdev, &fmt);

		if (remaining < count) {
			*fc = cap;
			*fi_index = remaining;
			return 0;
		}

		remaining -= count;
	}

	return -ENOENT;
}

static int mp_vid_object_enum_at(struct mp_vid_object *vid_obj, uint32_t index,
				 const struct mp_structure *filter, struct mp_structure *out)
{
	const struct video_format_cap *fc;
	struct mp_structure caps_item;
	uint32_t fi_index;
	int ret;

	ret = mp_vid_object_locate(vid_obj, index, &fc, &fi_index);
	if (ret != 0) {
		return ret;
	}

	ret = mp_vid_object_build_structure(vid_obj, fc, fi_index, &caps_item);
	if (ret != 0) {
		return ret;
	}

	return mp_pad_enum_filter(&caps_item, filter, out);
}

int mp_vid_object_enum_caps(struct mp_vid_object *vid_obj, uint32_t index,
			    const struct mp_structure *filter, struct mp_structure *out)
{
	int ret;

	if (vid_obj == NULL || out == NULL) {
		return -EINVAL;
	}

	/* An enumeration starts at index 0, which is where the bounds are refreshed */
	if (index == 0) {
		ret = mp_vid_object_probe_bounds(vid_obj);
		if (ret != 0) {
			return ret;
		}
	}

	return mp_vid_object_enum_at(vid_obj, index, filter, out);
}

/* True when the device advertises a frame interval of its own for this format */
static bool has_frmival(const struct device *vdev, struct video_format *fmt)
{
	struct video_frmival_enum fie = {.format = fmt};

	return video_enum_frmival(vdev, &fie) == 0;
}

int mp_vid_object_set_caps(struct mp_vid_object *vid_obj, const struct mp_structure *caps)
{
	struct video_format fmt;
	const struct mp_value *frmival_us;

	if (caps == NULL || !mp_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	frmival_us = mp_structure_get_value(caps, MP_CAPS_FRAME_INTERVAL);

	/* Set format */
	int ret = mp_structure_to_format(caps, vid_obj->type, &fmt);

	if (ret < 0) {
		return ret;
	}

	if (video_set_compose_format(vid_obj->vdev, &fmt) != 0) {
		LOG_ERR("Unable to set format");
		return -EIO;
	}

	/* Set buffer pool size */
	vid_obj->pool.pool.config.size = fmt.size;

	/*
	 * Apply the frame interval by asking the video subsystem for the closest one the
	 * device actually supports. The closest match is good enough, which also absorbs
	 * the rounding error from carrying whole microseconds: an interval such as
	 * 1001/30000 for 29.97 fps does not survive the conversion exactly.
	 *
	 * Only a device that has an interval of its own gets one set. An m2m device such
	 * as a decoder advertises none, and the interval it sees in the negotiated caps is
	 * the camera's, carried down the chain by a field the intersection copies from
	 * whichever side has it.
	 */
	if (frmival_us != NULL && has_frmival(vid_obj->vdev, &fmt)) {
		struct video_frmival_enum fie = {
			.format = &fmt,
			.type = VIDEO_FRMIVAL_TYPE_DISCRETE,
			.discrete.numerator = mp_value_get_uint(frmival_us),
			.discrete.denominator = USEC_PER_SEC,
		};

		if (video_closest_frmival(vid_obj->vdev, &fie) < 0 ||
		    video_set_frmival(vid_obj->vdev, &fie.discrete) != 0) {
			LOG_ERR("Unable to set frame interval");
			return -EIO;
		}
	}

	return 0;
}

int mp_vid_object_set_property(struct mp_vid_object *vid_obj, uint32_t key, const void *val)
{
	switch (key) {
	case MP_PROP_VID_DEVICE:
	case MP_PROP_VID_CROP:
		if (key == MP_PROP_VID_DEVICE) {
			vid_obj->vdev = val;
		} else {
			vid_obj->crop = *(struct video_rect *)val;

			/* Set crop selection target to HW */
			struct video_selection sel = {
				.type = vid_obj->type,
				.target = VIDEO_SEL_TGT_CROP,
				.rect = vid_obj->crop,
			};

			video_set_selection(vid_obj->vdev, &sel);
		}

		return 0;
	default:
		if (IN_RANGE(key, VIDEO_CID_BASE, VIDEO_CID_LASTP1) ||
		    IN_RANGE(key, VIDEO_CID_CODEC_CLASS_BASE, VIDEO_CID_JPEG_COMPRESSION_QUALITY) ||
		    key > VIDEO_CID_PRIVATE_BASE) {
			struct video_control ctrl = {.id = key, .val = (int32_t)(uintptr_t)val};

			return video_set_ctrl(vid_obj->vdev, &ctrl);
		}

		return -ENOTSUP;
	}
}

int mp_vid_object_get_property(struct mp_vid_object *vid_obj, uint32_t key, void *val)
{
	int ret;

	switch (key) {
	case MP_PROP_VID_DEVICE:
		*(const struct device **)val = vid_obj->vdev;
		return 0;
	case MP_PROP_VID_CROP:
		*(struct video_rect *)val = vid_obj->crop;
		return 0;
	default:
		if (IN_RANGE(key, VIDEO_CID_BASE, VIDEO_CID_LASTP1) ||
		    IN_RANGE(key, VIDEO_CID_CODEC_CLASS_BASE, VIDEO_CID_JPEG_COMPRESSION_QUALITY) ||
		    key > VIDEO_CID_PRIVATE_BASE) {
			struct video_control ctrl = {.id = key};

			ret = video_get_ctrl(vid_obj->vdev, &ctrl);
			if (ret < 0) {
				return ret;
			}

			*(int32_t *)val = ctrl.val;

			return 0;
		}

		return -ENOTSUP;
	}
}

int mp_vid_object_decide_allocation(struct mp_vid_object *vid_obj, struct mp_dispatch *query)
{
	struct mp_buffer_pool *query_pool = mp_dispatch_get_pool(query);
	struct mp_buffer_pool_config *pool_config = &vid_obj->pool.pool.config;
	struct mp_buffer_pool_config *qpc = NULL;

	if (query_pool == NULL) {
		qpc = mp_dispatch_get_pool_config(query);
	} else {
		qpc = &query_pool->config;
	}

	/* Always use its own pool, just negotiate the configs */
	if (qpc != NULL) {
		/* Decide min buffers */
		if (qpc->min_buffers > pool_config->min_buffers) {
			pool_config->min_buffers = qpc->min_buffers;
		}

		/* Decide alignment */
		int align = sys_lcm(qpc->align, pool_config->align);

		if (align == -1) {
			return -EINVAL;
		} else if (align == 0 && qpc->align != 0) {
			pool_config->align = qpc->align;
		} else if (align != 0) {
			pool_config->align = align;
		} else {
			/* align == 0 && qpc->align == 0: no change needed */
		}
	}

	return 0;
}
