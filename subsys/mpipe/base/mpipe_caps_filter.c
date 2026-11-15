/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

static int mpipe_caps_filter_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)obj;
	struct mpipe_caps_filter *filter = (struct mpipe_caps_filter *)obj;

	switch (key) {
	case MPIPE_PROP_BASE_CAPS_FILTER_CAPS:
		filter->filter_caps = *(const struct mpipe_structure *)val;
		mpipe_pad_set_caps(&transform->sink_pad, &filter->filter_caps);
		mpipe_pad_set_caps(&transform->src_pad, &filter->filter_caps);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mpipe_caps_filter_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_caps_filter *filter = (struct mpipe_caps_filter *)obj;

	switch (key) {
	case MPIPE_PROP_BASE_CAPS_FILTER_CAPS:
		*(struct mpipe_structure **)val = &filter->filter_caps;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mpipe_caps_filter_set_caps(struct mpipe_transform *transform,
				      enum mpipe_pad_direction direction,
				      const struct mpipe_structure *caps)
{
	struct mpipe_caps_filter *filter = (struct mpipe_caps_filter *)transform;
	int ret;
	struct mpipe_pad *upstream_src_pad = transform->sink_pad.peer;
	struct mpipe_pad *downstream_sink_pad = transform->src_pad.peer;

	ret = mpipe_transform_set_caps(transform, direction, caps);
	if (ret < 0) {
		return ret;
	}

	/*
	 * Take itself out of the graph once negotiated: buffers skip it and the pool
	 * query reaches its neighbors. The saved peers put it back on PAUSED -> READY.
	 */
	if (upstream_src_pad != NULL && downstream_sink_pad != NULL) {
		filter->saved_sink_peer = upstream_src_pad;
		filter->saved_src_peer = downstream_sink_pad;

		mpipe_pad_link(upstream_src_pad, downstream_sink_pad);
	}

	/* Drop the peer links, or the topological sort sees a cycle */
	transform->sink_pad.peer = NULL;
	transform->src_pad.peer = NULL;

	return 0;
}

static int mpipe_caps_filter_change_state(struct mpipe_element *self,
					  enum mpipe_state_change transition)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)self;
	struct mpipe_caps_filter *filter = (struct mpipe_caps_filter *)self;
	int ret;

	switch (transition) {
	case MPIPE_STATE_CHANGE_PAUSED_TO_READY:
		/* Put the element back so the next negotiation walks through it */
		if (filter->saved_sink_peer != NULL && filter->saved_src_peer != NULL) {
			mpipe_pad_link(filter->saved_sink_peer, &transform->sink_pad);
			mpipe_pad_link(&transform->src_pad, filter->saved_src_peer);

			filter->saved_sink_peer = NULL;
			filter->saved_src_peer = NULL;
		}
		break;
	default:
		break;
	}

	ret = mpipe_transform_change_state(self, transition);

	/* The base reset wiped the pads; the configured filter is not a negotiation result */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		mpipe_pad_set_caps(&transform->sink_pad, &filter->filter_caps);
		mpipe_pad_set_caps(&transform->src_pad, &filter->filter_caps);
	}

	return ret;
}

int mpipe_caps_filter_init(struct mpipe_caps_filter *caps_filter, uint8_t id)
{
	__ASSERT_NO_MSG(caps_filter != NULL);

	struct mpipe_element *self = &caps_filter->transform.element;
	struct mpipe_transform *transform = &caps_filter->transform;
	int ret = mpipe_transform_init(transform, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "caps_filter");

	ret = mpipe_structure_init_any(&caps_filter->filter_caps);
	if (ret != 0) {
		return ret;
	}

	self->object.set_property = mpipe_caps_filter_set_property;
	self->object.get_property = mpipe_caps_filter_get_property;
	self->change_state = mpipe_caps_filter_change_state;

	transform->mode = MPIPE_TRANSFORM_MODE_PASSTHROUGH;
	transform->set_caps = mpipe_caps_filter_set_caps;

	return 0;
}

#define DT_DRV_COMPAT zephyr_mpipe_caps_filter

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
/* A field at NULL or 0 is left out of the filter */
struct mpipe_caps_filter_dt_config {
	const char *pixel_format;
	uint32_t width;
	uint32_t height;
	uint32_t frame_rate;
	uint32_t frame_interval;
	uint32_t sample_rate;
	uint32_t bit_width;
	uint32_t num_channels;
	uint8_t media;
};

static int mpipe_caps_filter_dt_append(struct mpipe_structure *caps, uint8_t field, uint32_t uint)
{
	struct mpipe_value value;
	int ret = mpipe_value_set(&value, MPIPE_TYPE_UINT, uint);

	if (ret != 0) {
		return ret;
	}

	return mpipe_structure_append_value(caps, field, &value);
}

static int mpipe_caps_filter_dt_init(const struct mpipe_dt_element *dt)
{
	const struct mpipe_caps_filter_dt_config *config = dt->config;
	struct mpipe_structure caps;
	int ret = mpipe_caps_filter_init((struct mpipe_caps_filter *)dt->element, dt->id);

	if (ret != 0) {
		return ret;
	}

	ret = mpipe_structure_init(&caps, config->media);
	if (ret == 0 && config->pixel_format != NULL) {
		/* A pixel format is its four characters, the first one lowest */
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_PIXEL_FORMAT,
						  sys_get_le32(config->pixel_format));
	}
	if (ret == 0 && config->width != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_IMAGE_WIDTH, config->width);
	}
	if (ret == 0 && config->height != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_IMAGE_HEIGHT, config->height);
	}
	if (ret == 0 && config->frame_rate != 0U) {
		ret = mpipe_caps_filter_dt_append(
			&caps, MPIPE_CAPS_FRAME_INTERVAL,
			MPIPE_FRAME_INTERVAL_FROM_FPS(config->frame_rate));
	}
	if (ret == 0 && config->frame_interval != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_FRAME_INTERVAL,
						  config->frame_interval);
	}
	if (ret == 0 && config->sample_rate != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_SAMPLE_RATE,
						  config->sample_rate);
	}
	if (ret == 0 && config->bit_width != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_BIT_WIDTH, config->bit_width);
	}
	if (ret == 0 && config->num_channels != 0U) {
		ret = mpipe_caps_filter_dt_append(&caps, MPIPE_CAPS_NUM_CHANNELS,
						  config->num_channels);
	}
	if (ret != 0) {
		return ret;
	}

	return mpipe_object_set_properties(&dt->element->object, MPIPE_PROP_BASE_CAPS_FILTER_CAPS,
					   &caps, MPIPE_PROP_LIST_END);
}

#define MPIPE_CAPS_FILTER_DT_DEFINE(inst)                                                          \
	BUILD_ASSERT(sizeof(DT_INST_PROP_OR(inst, pixel_format, "none")) == 5,                     \
		     "pixel-format is a four-character code");                                     \
	BUILD_ASSERT(!DT_INST_NODE_HAS_PROP(inst, frame_rate) ||                                   \
			     !DT_INST_NODE_HAS_PROP(inst, frame_interval),                         \
		     "frame-rate and frame-interval set the same field");                          \
	static struct mpipe_caps_filter mpipe_caps_filter_##inst;                                  \
	static const struct mpipe_caps_filter_dt_config mpipe_caps_filter_dt_config_##inst = {     \
		.pixel_format = DT_INST_PROP_OR(inst, pixel_format, NULL),                         \
		.width = DT_INST_PROP_OR(inst, width, 0),                                          \
		.height = DT_INST_PROP_OR(inst, height, 0),                                        \
		.frame_rate = DT_INST_PROP_OR(inst, frame_rate, 0),                                \
		.frame_interval = DT_INST_PROP_OR(inst, frame_interval, 0),                        \
		.sample_rate = DT_INST_PROP_OR(inst, sample_rate, 0),                              \
		.bit_width = DT_INST_PROP_OR(inst, bit_width, 0),                                  \
		.num_channels = DT_INST_PROP_OR(inst, num_channels, 0),                            \
		.media = _CONCAT(MPIPE_MEDIA_, DT_INST_STRING_UPPER_TOKEN(inst, media)),           \
	};                                                                                         \
	MPIPE_DT_INST_ELEMENT_DEFINE(inst, &mpipe_caps_filter_##inst, mpipe_caps_filter_dt_init,   \
				     &mpipe_caps_filter_dt_config_##inst);

DT_INST_FOREACH_STATUS_OKAY(MPIPE_CAPS_FILTER_DT_DEFINE)
#endif
