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
	const mp_value_t value = mp_structure_get_value(structure, key);

	if (value == NULL) {
		return -EINVAL;
	}

	if (mp_value_get_type(value) == MP_TYPE_RANGE) {
		*min = mp_value_get_range_min(value);
		*max = mp_value_get_range_max(value);
		*step = (uint16_t)mp_value_get_range_step(value);
	} else if (mp_value_get_type(value) == MP_TYPE_INT) {
		*min = mp_value_get_int(value);
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
	mp_value_t value;

	/* Get pixel format field */
	value = mp_structure_get_value(structure, MP_CAPS_PIXEL_FORMAT);
	if (value == NULL) {
		return -EINVAL;
	}
	if (mp_value_get_type(value) == MP_TYPE_INT) {
		vfc->pixelformat = mp_value_get_int(value);
	} else if (mp_value_get_type(value) == MP_TYPE_LIST) {
		/* Format may be of MP_TYPE_LIST due to the intersection with a list type but it is
		 * actually a single-value list, so take the 1st item in the list
		 */
		vfc->pixelformat = mp_value_get_int(mp_value_list_get(value, 0));
	} else {
		return -EINVAL;
	}

	/* Get width fields */
	ret = set_dimension_fields(structure, MP_CAPS_IMAGE_WIDTH,
				   &vfc->width_min, &vfc->width_max, &vfc->width_step);
	if (ret < 0) {
		return ret;
	}

	/* Get height fields */
	return set_dimension_fields(structure, MP_CAPS_IMAGE_HEIGHT, &vfc->height_min,
				    &vfc->height_max, &vfc->height_step);
}

static int append_frmrates_to_structure(const struct device *vdev, struct video_format *fmt,
					struct mp_structure *caps_item)
{
	mp_value_t frmrates;
	mp_value_t frmrate = NULL;

	size_t length = 0;
	for (struct video_frmival_enum fie = {.format = fmt};
	     video_enum_frmival(vdev, &fie) == 0;
	     fie.index++) {
		length++;
	}

	frmrates = mp_value_new_list(length, NULL);
	if (frmrates == NULL) {
		goto nomem;
	}

	for (struct video_frmival_enum fie = {.format = fmt};
	     video_enum_frmival(vdev, &fie) == 0;
	     fie.index++) {
		switch (fie.type) {
		case VIDEO_FRMIVAL_TYPE_DISCRETE:
			frmrate = mp_value_new_int(video_frmival_nsec(&fie.discrete));
			if (frmrate == NULL) {
				goto nomem;
			}

			mp_value_list_set(frmrates, fie.index, frmrate);
			break;
		case VIDEO_FRMIVAL_TYPE_STEPWISE:
			frmrate = mp_value_new_range(video_frmival_nsec(&fie.stepwise.min),
						     video_frmival_nsec(&fie.stepwise.max),
						     video_frmival_nsec(&fie.stepwise.step));
			if (frmrate == NULL) {
				goto nomem;
			}

			mp_value_list_set(frmrates, fie.index, frmrate);
			break;
		default:
			break;
		}
	}

	if (!mp_value_list_is_empty(frmrates)) {
		mp_structure_append(caps_item, MP_CAPS_FRAME_RATE, frmrates);
	}

	return 0;
nomem:
	mp_value_unref(frmrate);
	mp_value_unref(frmrates);
	return -ENOMEM;
}

struct mp_vid_object_caps {
	struct mp_caps base;
	struct mp_vid_object *vid_obj;
};

struct mp_caps *mp_vid_object_caps_destroy(struct mp_object *obj)
{
	struct mp_vid_object_caps *caps = (struct mp_vid_object_caps *)obj;

	mp_object_unref((struct mp_object *)caps->vid_obj);
	k_free(obj);
}

struct mp_caps *mp_vid_object_caps_get_structure(struct mp_caps *caps_in)
{
	struct mp_vid_object_caps *caps = caps_in;
}

struct mp_caps *mp_vid_object_caps_new(struct mp_vid_object *vid_obj)
{
	struct mp_vid_object_caps *caps = k_calloc(1, sizeof(*caps));
	if (caps == NULL) {
		return NULL;
	}

	mp_caps_init(&caps->base, 0);
	caps->base.object.release = mp_vid_object_caps_destroy;
	caps->base.get_structure = mp_vid_object_caps_get_structure;
	caps->vid_obj = vid_obj;

	return mp_object_ref((struct mp_object *)obj);
}

struct mp_caps *mp_vid_object_get_caps(struct mp_vid_object *vid_obj)
{
	int ret;
	struct mp_caps *caps = mp_vid_object_caps_new(vid_obj);
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
			MP_MEDIA_VIDEO,
			MP_CAPS_PIXEL_FORMAT, MP_INT(vcaps.format_caps[i].pixelformat),
			MP_CAPS_IMAGE_WIDTH, MP_RANGE(
				min3(vcaps.format_caps[i].width_min, crop_w, comp_min_w),
				max3(vcaps.format_caps[i].width_max, crop_w, comp_max_w),
				vcaps.format_caps[i].width_step),
			MP_CAPS_IMAGE_HEIGHT, MP_RANGE(
				min3(vcaps.format_caps[i].height_min, crop_h, comp_min_h),
				max3(vcaps.format_caps[i].height_max, crop_h, comp_max_h),
				vcaps.format_caps[i].height_step),
			MP_CAPS_END);

		/* Get frame rate */
		fmt.pixelformat = vcaps.format_caps[i].pixelformat;
		fmt.width = vcaps.format_caps[i].width_min;
		fmt.height = vcaps.format_caps[i].height_min;
		append_frmrates_to_structure(vid_obj->vdev, &fmt, caps_item);

		mp_caps_append(caps, caps_item);
	}

	return caps;
}

int mp_vid_object_set_caps(struct mp_vid_object *vid_obj, struct mp_caps *caps)
{
	struct video_format_cap vfc = {0};
	struct video_format fmt;
	struct video_frmival frmival;
	struct mp_structure *first_structure = mp_caps_get_structure(caps, 0);
	mp_value_t frmrate = mp_structure_get_value(first_structure, MP_CAPS_FRAME_RATE);

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

	/* Set frame rate only if the element's caps support it */
	struct mp_caps *objcaps = mp_vid_object_get_caps(vid_obj);

	first_structure = mp_caps_get_structure(objcaps, 0);
	if (frmrate != NULL &&
	    mp_structure_get_value(first_structure, MP_CAPS_FRAME_RATE) != NULL) {
		frmival.numerator = NSEC_PER_SEC;
		frmival.denominator = mp_value_get_int(frmrate);
		ret = video_set_frmival(vid_obj->vdev, &frmival);
		if (ret) {
			LOG_ERR("Unable to set frame interval");
		}
	}

	mp_caps_unref(objcaps);
	return ret;
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
