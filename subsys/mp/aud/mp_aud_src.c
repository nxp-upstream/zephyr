/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/aud/mp_aud_buffer_pool.h>
#include <zephyr/mp/aud/mp_aud_src.h>

LOG_MODULE_REGISTER(mp_aud_src, CONFIG_MP_LOG_LEVEL);

static int mp_aud_src_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_src *src = (struct mp_src *)obj;
	struct mp_aud_buffer_pool *pool = CONTAINER_OF(src->pool, struct mp_aud_buffer_pool, pool);

	switch (key) {
	case MP_PROP_AUD_SRC_SLAB_PTR:
		pool->mem_slab = (struct k_mem_slab *)val;
		break;
	case MP_PROP_AUD_SRC_DEVICE:
		pool->aud_dev = (const struct device *)val;

		/* Device set, update supported caps */
		mp_aud_src_update_caps(src);
		break;
	default:
		return mp_src_set_property(obj, key, val);
	}

	return 0;
}

static int mp_aud_src_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_src *src = (struct mp_src *)obj;
	struct mp_aud_buffer_pool *pool = CONTAINER_OF(src->pool, struct mp_aud_buffer_pool, pool);

	if (val == NULL) {
		return -1;
	}

	switch (key) {
	case MP_PROP_AUD_SRC_SLAB_PTR:
		if (pool->mem_slab != NULL) {
			*(void **)val = (void *)pool->mem_slab;
		} else {
			*(void **)val = NULL;
		}
		break;
	case MP_PROP_AUD_SRC_DEVICE:
		*(const struct device **)val = pool->aud_dev;
		break;
	default:
		return mp_src_get_property(obj, key, val);
	}

	return 0;
}

/*
 * A device describes its sample rates and bit widths as two masks, and every
 * combination of them is a capability of its own. The enumeration index spans
 * both, so each capability is a plain fixed structure instead of one structure
 * holding two list values.
 */
static int mp_aud_src_enum_caps(struct mp_pad *pad, uint32_t index,
				const struct mp_structure *filter, struct mp_structure *out)
{
	struct mp_src *src = (struct mp_src *)pad->object.container;
	struct mp_aud_src *aud_src = (struct mp_aud_src *)src;
	struct mp_aud_buffer_pool *pool = CONTAINER_OF(src->pool, struct mp_aud_buffer_pool, pool);
	struct mp_structure candidate;
	struct audio_caps src_caps;
	uint32_t num_widths;
	uint32_t sample_rate;
	uint32_t bit_width;
	int ret;

	if (aud_src->get_audio_caps == NULL || pool->aud_dev == NULL) {
		LOG_ERR("Audio capabilities and device not configured");
		return -ENODEV;
	}

	if (aud_src->get_audio_caps(pool->aud_dev, &src_caps) != 0) {
		LOG_ERR("Failed to get audio capabilities");
		return -ENODEV;
	}

	num_widths = mp_aud_count_bit_widths(src_caps.supported_bit_widths);
	if (num_widths == 0U) {
		return -ENOENT;
	}

	sample_rate = mp_aud_nth_sample_rate(src_caps.supported_sample_rates, index / num_widths);
	if (sample_rate == 0U) {
		return -ENOENT;
	}

	bit_width = mp_aud_nth_bit_width(src_caps.supported_bit_widths, index % num_widths);
	if (bit_width == 0U) {
		return -ENOENT;
	}

	ret = mp_structure_init_fields(
		&candidate, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE, MP_TYPE_UINT, sample_rate,
		MP_CAPS_BITWIDTH, MP_TYPE_UINT, bit_width, MP_CAPS_NUM_OF_CHANNEL,
		MP_TYPE_UINT_RANGE, src_caps.min_total_channels, src_caps.max_total_channels, 1,
		MP_CAPS_FRAME_INTERVAL, MP_TYPE_UINT_RANGE, src_caps.min_frame_interval,
		src_caps.max_frame_interval, 1, MP_CAPS_BUFFER_COUNT, MP_TYPE_UINT_RANGE,
		src_caps.min_num_buffers, UINT8_MAX, 1, MP_CAPS_INTERLEAVED, MP_TYPE_BOOLEAN,
		src_caps.interleaved, MP_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	return mp_pad_enum_filter(&candidate, filter, out);
}

void mp_aud_src_update_caps(struct mp_src *src)
{
	/* The capabilities are enumerated from the device, so nothing is built here */
	src->srcpad.enum_capsfn = mp_aud_src_enum_caps;
}

void mp_aud_src_init(struct mp_element *self)
{
	struct mp_aud_src *aud_src = (struct mp_aud_src *)self;

	/* Init base class */
	mp_src_init(&aud_src->src.element);

	self->object.get_property = mp_aud_src_get_property;
	self->object.set_property = mp_aud_src_set_property;

	aud_src->get_audio_caps = NULL;
}
