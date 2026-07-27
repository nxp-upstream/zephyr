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

#include <zephyr/mp/mp_caps.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/vid/mp_vid_object.h>
#include <zephyr/mp/vid/mp_vid_property.h>

LOG_MODULE_REGISTER(mp_vid_object, CONFIG_MP_LOG_LEVEL);

static int set_dimension_fields(struct mp_structure *structure, uint8_t key, uint32_t *min,
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

int mp_structure_to_vfc(struct mp_structure *structure, struct video_format_cap *vfc)
{
	int ret;
	struct mp_value *value;

	/* Get pixel format field */
	value = mp_structure_get_value(structure, MP_CAPS_PIXEL_FORMAT);
	if (value == NULL) {
		return -EINVAL;
	}
	if (value->type == MP_TYPE_UINT) {
		vfc->pixelformat = mp_value_get_uint(value);
	} else if (value->type == MP_TYPE_LIST) {
		/* Format may be of MP_TYPE_LIST due to the intersection with a list type but it is
		 * actually a single-value list, so take the 1st item in the list
		 */
		vfc->pixelformat = mp_value_get_uint(mp_value_list_get(value, 0));
	} else {
		return -EINVAL;
	}

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

bool mp_vid_caps_has_vfc(struct mp_caps *caps, const struct video_format_cap *vfc)
{
	struct mp_cap_structure *cs;
	struct video_format_cap existing;

	if (caps == NULL || vfc == NULL) {
		return false;
	}

	SYS_SLIST_FOR_EACH_CONTAINER(&caps->caps_structures, cs, node) {
		if (mp_structure_to_vfc(cs->structure, &existing) < 0) {
			continue;
		}

		if (existing.pixelformat == vfc->pixelformat &&
		    existing.width_min == vfc->width_min && existing.width_max == vfc->width_max &&
		    existing.width_step == vfc->width_step &&
		    existing.height_min == vfc->height_min &&
		    existing.height_max == vfc->height_max &&
		    existing.height_step == vfc->height_step) {
			return true;
		}
	}

	return false;
}

static uint32_t frmival_to_usec(const struct video_frmival *frmival)
{
	if (frmival->denominator == 0) {
		return 0;
	}

	return (uint32_t)DIV_ROUND_CLOSEST((uint64_t)frmival->numerator * USEC_PER_SEC,
					   frmival->denominator);
}

static void append_frmivals_to_structure(const struct device *vdev, struct video_format *fmt,
					 struct mp_structure *caps_item)
{
	struct mp_value *frmivals = NULL;
	struct mp_value *frmival;
	struct video_frmival_enum fie = {0};

	fie.format = fmt;
	while (video_enum_frmival(vdev, &fie) == 0) {
		if (fie.type == VIDEO_FRMIVAL_TYPE_STEPWISE) {
			/*
			 * A stepwise device describes all its intervals with a single range,
			 * so it cannot be mixed with discrete ones in the same field. Drop
			 * anything collected so far and stop enumerating.
			 */
			frmival =
				mp_value_new(MP_TYPE_UINT_RANGE, frmival_to_usec(&fie.stepwise.min),
					     frmival_to_usec(&fie.stepwise.max),
					     frmival_to_usec(&fie.stepwise.step));

			mp_value_destroy(frmivals);
			frmivals = frmival;
			break;
		}

		if (fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
			if (frmivals == NULL) {
				frmivals = mp_value_new(MP_TYPE_LIST, NULL);
				if (frmivals == NULL) {
					return;
				}
			}

			frmival = mp_value_new(MP_TYPE_UINT, frmival_to_usec(&fie.discrete));

			if (mp_value_list_append(frmivals, frmival) < 0) {
				mp_value_destroy(frmival);
			}
		}

		fie.index++;
	}

	/* Devices without frame interval support report nothing at all */
	if (frmivals == NULL) {
		return;
	}

	(void)mp_structure_append(caps_item, MP_CAPS_FRAME_INTERVAL, frmivals);
}

struct mp_caps *mp_vid_object_get_caps(struct mp_vid_object *vid_obj)
{
	int ret;
	struct mp_caps *caps = mp_caps_new(MP_MEDIA_END);
	struct mp_structure *caps_item = NULL;
	struct video_caps vcaps = {.type = vid_obj->type};
	struct video_format fmt = {.type = vid_obj->type};
	struct video_rect rect;
	uint32_t crop_w = UINT32_MAX;
	uint32_t crop_h = UINT32_MAX;
	uint32_t comp_min_w = UINT32_MAX;
	uint32_t comp_min_h = UINT32_MAX;
	uint32_t comp_max_w = 0;
	uint32_t comp_max_h = 0;

	struct video_selection sel = {
		.type = vid_obj->type,
		.target = VIDEO_SEL_TGT_CROP,
	};

	/* Get caps */
	if (video_get_caps(vid_obj->vdev, &vcaps)) {
		LOG_WRN("Unable to retrieve device's capabilities");
		return NULL;
	}

	/* Get crop selection */
	ret = video_get_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		crop_w = sel.rect.width;
		crop_h = sel.rect.height;
	}

	/* Get compose selection upper-bound */
	sel.target = VIDEO_SEL_TGT_COMPOSE_BOUND;
	ret = video_get_selection(vid_obj->vdev, &sel);
	if (ret == 0) {
		comp_max_w = sel.rect.width + sel.rect.left;
		comp_max_h = sel.rect.height + sel.rect.top;
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
		comp_min_w = sel.rect.width + sel.rect.left;
		comp_min_h = sel.rect.height + sel.rect.top;
	}

	/* Set back the original compose selection */
	sel.rect = rect;
	video_set_selection(vid_obj->vdev, &sel);

	/* Set buffer pool's min_buffers and alignment */
	vid_obj->pool.pool.config.min_buffers = vcaps.min_vbuf_count;
	vid_obj->pool.pool.config.align = vcaps.buf_align;

	for (uint8_t i = 0; vcaps.format_caps[i].pixelformat != 0; i++) {
		caps_item = mp_structure_new(
			MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT,
			vcaps.format_caps[i].pixelformat, MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT_RANGE,
			min3(vcaps.format_caps[i].width_min, crop_w, comp_min_w),
			max(vcaps.format_caps[i].width_max, comp_max_w),
			vcaps.format_caps[i].width_step, MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT_RANGE,
			min3(vcaps.format_caps[i].height_min, crop_h, comp_min_h),
			max(vcaps.format_caps[i].height_max, comp_max_h),
			vcaps.format_caps[i].height_step, MP_CAPS_END);

		/* Get frame interval */
		fmt.pixelformat = vcaps.format_caps[i].pixelformat;
		fmt.width = vcaps.format_caps[i].width_min;
		fmt.height = vcaps.format_caps[i].height_min;
		append_frmivals_to_structure(vid_obj->vdev, &fmt, caps_item);

		mp_caps_append(caps, caps_item);
	}

	return caps;
}

/* True when the device advertises a frame interval of its own for this format */
static bool has_frmival(const struct device *vdev, struct video_format *fmt)
{
	struct video_frmival_enum fie = {.format = fmt};

	return video_enum_frmival(vdev, &fie) == 0;
}

int mp_vid_object_set_caps(struct mp_vid_object *vid_obj, struct mp_caps *caps)
{
	struct video_format_cap vfc = {0};
	struct video_format fmt;
	struct mp_structure *first_structure = mp_caps_get_structure(caps, 0);
	struct mp_value *frmival_us =
		mp_structure_get_value(first_structure, MP_CAPS_FRAME_INTERVAL);

	if (!mp_caps_is_fixed(caps)) {
		return -EINVAL;
	}

	/* Set format */
	int ret = mp_structure_to_vfc(first_structure, &vfc);

	if (ret < 0) {
		return ret;
	}

	fmt.type = vid_obj->type;
	fmt.pixelformat = vfc.pixelformat;
	fmt.width = vfc.width_min;
	fmt.height = vfc.height_min;
	if (video_set_compose_format(vid_obj->vdev, &fmt)) {
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
