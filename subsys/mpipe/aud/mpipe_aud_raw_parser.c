/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_structure.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_raw_parser.h>

LOG_MODULE_REGISTER(mpipe_aud_raw_parser, CONFIG_MPIPE_LOG_LEVEL);

/*
 * The format comes from the properties, so the capability is built on demand
 * rather than kept on the pad, which the base resets on every teardown.
 */
static int mpipe_aud_raw_parser_enum_caps(struct mpipe_pad *pad, uint32_t index,
					  const struct mpipe_structure *filter,
					  struct mpipe_structure *out)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)pad->object.container;
	struct mpipe_structure candidate;
	int ret;

	if (index > 0) {
		return -ENOENT;
	}

	/* The sink accepts anything, so the source is the only side to describe */
	if (pad->direction == MPIPE_PAD_SINK) {
		if (filter != NULL) {
			*out = *filter;
			return 0;
		}

		return mpipe_structure_init_any(out);
	}

	ret = mpipe_structure_init_fields(
		&candidate, MPIPE_MEDIA_AUDIO_PCM, MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT,
		parser->sample_rate, MPIPE_CAPS_BIT_WIDTH, MPIPE_TYPE_UINT, parser->bit_width,
		MPIPE_CAPS_NUM_CHANNELS, MPIPE_TYPE_UINT, parser->num_of_channel,
		MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT, parser->frame_interval,
		MPIPE_CAPS_INTERLEAVED, MPIPE_TYPE_BOOLEAN, true, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_aud_raw_parser_set_format(struct mpipe_aud_raw_parser *parser, uint32_t key,
					   const void *val)
{
	uint32_t v;

	if (val == NULL) {
		return -EINVAL;
	}

	if (parser->base.element.current_state != MPIPE_STATE_READY) {
		LOG_ERR("The format only changes in the READY state");
		return -EBUSY;
	}

	v = *(const uint32_t *)val;

	switch (key) {
	case MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE:
		if (v == 0U) {
			return -EINVAL;
		}
		parser->sample_rate = v;
		break;
	case MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH:
		if (v != MPIPE_AUD_BIT_WIDTH_16 && v != MPIPE_AUD_BIT_WIDTH_24 &&
		    v != MPIPE_AUD_BIT_WIDTH_32) {
			return -EINVAL;
		}
		parser->bit_width = v;
		break;
	case MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS:
		if (v == 0U) {
			return -EINVAL;
		}
		parser->num_of_channel = v;
		break;
	case MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL:
		if (v == 0U) {
			return -EINVAL;
		}
		parser->frame_interval = v;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int mpipe_aud_raw_parser_set_property(struct mpipe_object *obj, uint32_t key,
					     const void *val)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)obj;

	switch (key) {
	case MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR:
		parser->out_pool.mem_slab = (struct k_mem_slab *)val;
		return 0;
	case MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE:
	case MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH:
	case MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS:
	case MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL:
		return mpipe_aud_raw_parser_set_format(parser, key, val);
	default:
		LOG_ERR("Property %u is unknown", key);
		return -ENOTSUP;
	}
}

static int mpipe_aud_raw_parser_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR:
		*(struct k_mem_slab **)val = parser->out_pool.mem_slab;
		return 0;
	case MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE:
		*(uint32_t *)val = parser->sample_rate;
		return 0;
	case MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH:
		*(uint32_t *)val = parser->bit_width;
		return 0;
	case MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS:
		*(uint32_t *)val = parser->num_of_channel;
		return 0;
	case MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL:
		*(uint32_t *)val = parser->frame_interval;
		return 0;
	default:
		LOG_ERR("Property %u is unknown", key);
		return -ENOTSUP;
	}
}

/*
 * The blocks come from this element's pool: downstream's demand plus the one
 * block being filled. The block size follows from the negotiated format when
 * the base configures the pool.
 */
static int mpipe_aud_raw_parser_decide_buffer_pool(struct mpipe_parser *base,
						   struct mpipe_dispatch *query)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)base;
	const struct mpipe_buffer_pool_config *qpc =
		(query->pool != NULL) ? &query->pool->config : &query->pool_cfg;

	if (parser->out_pool.mem_slab == NULL) {
		LOG_ERR("No memory slab set for the output blocks");
		return -EINVAL;
	}

	parser->out_pool.pool.config.min_buffers = qpc->min_buffers + 1U;
	base->out_pool = &parser->out_pool.pool;

	return 0;
}

/*
 * Copy the input into blocks of the negotiated size. A block always starts
 * on a sample frame, since its size is a whole number of them. Bytes for
 * which no block is free are dropped, which keeps the thread feeding the
 * sink from blocking on a stream that is late anyway.
 */
static int mpipe_aud_raw_parser_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					   struct net_buf **out_buf)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)pad->object.container;
	struct mpipe_buffer_pool *pool = parser->base.out_pool;
	const uint8_t *src = in_buf->data;
	uint32_t remaining = mpipe_buffer_get_meta(in_buf)->bytes_used;

	*out_buf = NULL;

	while (remaining > 0U) {
		struct mpipe_buffer_meta *bm;
		uint32_t n;

		if (parser->block == NULL &&
		    (pool->acquire_buffer(pool, &parser->block) != 0 || parser->block == NULL)) {
			LOG_WRN("Dropping %u bytes of audio, no block free", remaining);
			parser->block = NULL;
			break;
		}

		bm = mpipe_buffer_get_meta(parser->block);
		n = MIN(pool->config.size - bm->bytes_used, remaining);
		memcpy(parser->block->data + bm->bytes_used, src, n);
		bm->bytes_used += n;
		parser->block->len = bm->bytes_used;
		src += n;
		remaining -= n;

		if (bm->bytes_used == pool->config.size) {
			bm->timestamp = k_uptime_get_32();
			if (*out_buf == NULL) {
				*out_buf = parser->block;
			} else {
				net_buf_frag_add(*out_buf, parser->block);
			}
			parser->block = NULL;
		}
	}

	net_buf_unref(in_buf);

	return 0;
}

static int mpipe_aud_raw_parser_change_state(struct mpipe_element *self,
					     enum mpipe_state_change transition)
{
	struct mpipe_aud_raw_parser *parser = (struct mpipe_aud_raw_parser *)self;

	/* The block left half full on teardown is not worth playing */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY && parser->block != NULL) {
		net_buf_unref(parser->block);
		parser->block = NULL;
	}

	/* The base resets the negotiated pad caps and stops the output pool */
	return mpipe_parser_change_state(self, transition);
}

int mpipe_aud_raw_parser_init(struct mpipe_aud_raw_parser *parser, uint8_t id)
{
	__ASSERT_NO_MSG(parser != NULL);

	struct mpipe_element *self = &parser->base.element;
	struct mpipe_parser *base = &parser->base;
	int ret = mpipe_parser_init(base, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "aud_raw_parser");

	self->change_state = mpipe_aud_raw_parser_change_state;
	self->object.set_property = mpipe_aud_raw_parser_set_property;
	self->object.get_property = mpipe_aud_raw_parser_get_property;

	base->sink_pad.enum_caps_fn = mpipe_aud_raw_parser_enum_caps;
	base->src_pad.enum_caps_fn = mpipe_aud_raw_parser_enum_caps;
	base->sink_pad.process_fn = mpipe_aud_raw_parser_process_fn;
	base->decide_buffer_pool = mpipe_aud_raw_parser_decide_buffer_pool;

	parser->block = NULL;
	parser->sample_rate = MPIPE_AUD_SAMPLE_RATE_48000;
	parser->bit_width = MPIPE_AUD_BIT_WIDTH_16;
	parser->num_of_channel = 2;
	parser->frame_interval = 10000;

	mpipe_aud_buffer_pool_init(&parser->out_pool.pool);
	base->out_pool = &parser->out_pool.pool;

	return 0;
}
