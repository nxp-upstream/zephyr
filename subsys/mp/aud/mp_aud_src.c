/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>

#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_dispatch.h>
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

static int mp_aud_src_enum_caps(struct mp_pad *pad, uint32_t index,
				const struct mp_structure *filter, struct mp_structure *out)
{
	struct mp_src *src = (struct mp_src *)pad->object.container;
	struct mp_aud_src *aud_src = (struct mp_aud_src *)src;
	struct mp_aud_buffer_pool *pool = CONTAINER_OF(src->pool, struct mp_aud_buffer_pool, pool);
	struct audio_caps src_caps;

	if (aud_src->get_audio_caps == NULL || pool->aud_dev == NULL) {
		LOG_ERR("Audio capabilities and device not configured");
		return -ENODEV;
	}

	if (aud_src->get_audio_caps(pool->aud_dev, &src_caps) != 0) {
		LOG_ERR("Failed to get audio capabilities");
		return -ENODEV;
	}

	return mp_aud_enum_caps(&src_caps, index, filter, out);
}

void mp_aud_src_update_caps(struct mp_src *src)
{
	/* The capabilities are enumerated from the device, so nothing is built here */
	src->srcpad.enum_capsfn = mp_aud_src_enum_caps;
}

/*
 * Buffer count is not a media capability, so it is settled through the
 * allocation query instead of caps: the pool is floored at what the source
 * device needs to keep streaming and raised to what downstream must hold in
 * flight, whichever is larger.
 */
static int mp_aud_src_decide_allocation(struct mp_src *src, struct mp_dispatch *query)
{
	struct mp_aud_src *aud_src = (struct mp_aud_src *)src;
	struct mp_aud_buffer_pool *pool = CONTAINER_OF(src->pool, struct mp_aud_buffer_pool, pool);
	struct mp_buffer_pool_config *pool_config = &src->pool->config;
	struct mp_buffer_pool *query_pool = mp_dispatch_get_pool(query);
	struct mp_buffer_pool_config *qpc =
		(query_pool != NULL) ? &query_pool->config : mp_dispatch_get_pool_config(query);
	struct audio_caps src_caps;

	/* Floor the pool at the source device's own buffering requirement */
	if (aud_src->get_audio_caps != NULL && pool->aud_dev != NULL &&
	    aud_src->get_audio_caps(pool->aud_dev, &src_caps) == 0) {
		pool_config->min_buffers = src_caps.min_num_buffers;
	} else {
		pool_config->min_buffers = 0;
	}

	/* Raise it to what downstream needs held in flight, if higher */
	if (qpc != NULL && qpc->min_buffers > pool_config->min_buffers) {
		pool_config->min_buffers = qpc->min_buffers;
	}

	return 0;
}

void mp_aud_src_init(struct mp_element *self)
{
	struct mp_aud_src *aud_src = (struct mp_aud_src *)self;

	/* Init base class */
	mp_src_init(&aud_src->src.element);

	self->object.get_property = mp_aud_src_get_property;
	self->object.set_property = mp_aud_src_set_property;

	aud_src->src.decide_allocation = mp_aud_src_decide_allocation;

	aud_src->get_audio_caps = NULL;
}
