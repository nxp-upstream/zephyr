/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/video/controls.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/mpipe/mpipe_dt.h>

#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool.h>
#include <zephyr/mpipe/vid/mpipe_vid_property.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>

LOG_MODULE_REGISTER(mpipe_vid_src, CONFIG_MPIPE_LOG_LEVEL);

#define DEFAULT_PROP_DEVICE DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_camera))

static int mpipe_vid_src_enum_caps(struct mpipe_pad *pad, uint32_t index,
				   const struct mpipe_structure *filter,
				   struct mpipe_structure *out)
{
	struct mpipe_vid_src *vid_src = (struct mpipe_vid_src *)pad->object.container;

	return mpipe_vid_object_enum_caps(&vid_src->vid_obj, index, filter, out);
}

static int mpipe_vid_src_set_caps(struct mpipe_src *src, const struct mpipe_structure *caps)
{
	struct mpipe_vid_src *vid_src = (struct mpipe_vid_src *)src;

	if (mpipe_vid_object_set_caps(&vid_src->vid_obj, caps) < 0) {
		return -EINVAL;
	}

	/* Set pad's caps only when everything is OK */
	return mpipe_pad_set_caps(&src->src_pad, caps);
}

static int mpipe_vid_src_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_vid_src *vid_src = (struct mpipe_vid_src *)obj;
	int ret = mpipe_vid_object_set_property(&vid_src->vid_obj, key, val);

	if (ret == -ENOTSUP) {
		return mpipe_src_set_property(obj, key, val);
	}

	return ret;
}

static int mpipe_vid_src_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_vid_src *vid_src = (struct mpipe_vid_src *)obj;
	int ret = mpipe_vid_object_get_property(&vid_src->vid_obj, key, val);

	if (ret == -ENOTSUP) {
		return mpipe_src_get_property(obj, key, val);
	}

	return ret;
}

static int mpipe_vid_src_decide_buffer_pool(struct mpipe_src *self, struct mpipe_dispatch *query)
{
	struct mpipe_vid_src *vid_src = (struct mpipe_vid_src *)self;

	return mpipe_vid_object_decide_buffer_pool(&vid_src->vid_obj, query);
}

int mpipe_vid_src_init(struct mpipe_vid_src *vid_src, uint8_t id)
{
	__ASSERT_NO_MSG(vid_src != NULL);

	struct mpipe_element *self = &vid_src->src.element;
	struct mpipe_src *src = &vid_src->src;
	int ret = mpipe_src_init(src, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_src");

	/* Initialize vid object */
	vid_src->vid_obj.element = self;
	vid_src->vid_obj.vdev = DEFAULT_PROP_DEVICE;
	vid_src->vid_obj.type = VIDEO_BUF_TYPE_OUTPUT;

	self->object.get_property = mpipe_vid_src_get_property;
	self->object.set_property = mpipe_vid_src_set_property;

	/*
	 * The pool has to exist before the first capability is enumerated, as
	 * probing the device fills in the pool's buffer count and alignment.
	 */
	src->pool = &vid_src->vid_obj.pool.pool;
	mpipe_vid_buffer_pool_init(src->pool, &vid_src->vid_obj);

	src->src_pad.enum_caps_fn = mpipe_vid_src_enum_caps;
	src->set_caps = mpipe_vid_src_set_caps;
	src->decide_buffer_pool = mpipe_vid_src_decide_buffer_pool;

	return 0;
}

#define DT_DRV_COMPAT zephyr_mpipe_vid_src

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
struct mpipe_vid_src_dt_config {
	/* NULL keeps the default */
	const struct device *dev;
	/* A width of 0 keeps the default */
	struct video_rect crop;
	/* 0 keeps the default */
	uint32_t num_buffers;
	bool hflip;
	bool vflip;
};

static int mpipe_vid_src_dt_init(const struct mpipe_dt_element *dt)
{
	const struct mpipe_vid_src_dt_config *config = dt->config;
	int ret = mpipe_vid_src_init((struct mpipe_vid_src *)dt->element, dt->id);

	/* The device first: the other properties are applied to it */
	if (ret == 0 && config->dev != NULL) {
		ret = mpipe_object_set_properties(&dt->element->object, MPIPE_PROP_VID_DEVICE,
						  config->dev, MPIPE_PROP_LIST_END);
	}
	if (ret == 0 && config->crop.width != 0U) {
		ret = mpipe_object_set_properties(&dt->element->object, MPIPE_PROP_VID_CROP,
						  &config->crop, MPIPE_PROP_LIST_END);
	}
	if (ret == 0 && config->hflip) {
		ret = mpipe_object_set_properties(&dt->element->object, VIDEO_CID_HFLIP,
						  (const void *)(uintptr_t)1, MPIPE_PROP_LIST_END);
	}
	if (ret == 0 && config->vflip) {
		ret = mpipe_object_set_properties(&dt->element->object, VIDEO_CID_VFLIP,
						  (const void *)(uintptr_t)1, MPIPE_PROP_LIST_END);
	}
	if (ret != 0 || config->num_buffers == 0U) {
		return ret;
	}

	return mpipe_object_set_properties(&dt->element->object, MPIPE_PROP_SRC_NUM_BUFS,
					   (const void *)(uintptr_t)config->num_buffers,
					   MPIPE_PROP_LIST_END);
}

#define MPIPE_VID_SRC_DT_DEFINE(inst)                                                              \
	static struct mpipe_vid_src mpipe_vid_src_##inst;                                          \
	static const struct mpipe_vid_src_dt_config mpipe_vid_src_dt_config_##inst = {             \
		.dev = MPIPE_DT_INST_DEVICE_GET_OR_NULL(inst, device),                             \
		.crop = DT_INST_PROP_OR(inst, crop, {0}),                                          \
		.num_buffers = DT_INST_PROP_OR(inst, num_buffers, 0),                              \
		.hflip = DT_INST_PROP(inst, hflip),                                                \
		.vflip = DT_INST_PROP(inst, vflip),                                                \
	};                                                                                         \
	MPIPE_DT_INST_ELEMENT_DEFINE(inst, &mpipe_vid_src_##inst, mpipe_vid_src_dt_init,           \
				     &mpipe_vid_src_dt_config_##inst);

DT_INST_FOREACH_STATUS_OKAY(MPIPE_VID_SRC_DT_DEFINE)
#endif
