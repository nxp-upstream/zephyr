/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_transform_client.h>

/* Investigation aid, see mpipe_stage_stats.h */
#define MPIPE_STAGE_STATS IS_ENABLED(CONFIG_MPIPE_STAGE_STATS)
#if MPIPE_STAGE_STATS
#include "mpipe_stage_stats.h"
static struct mpipe_stage_stats client_stats;
#endif

LOG_MODULE_REGISTER(mpipe_transform_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_transform_client_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					     struct net_buf **out_buf)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)pad->object.container;
	struct mpipe_transform_client *transform_client =
		(struct mpipe_transform_client *)transform;
	struct mpipe_buffer_meta *in_meta;
	struct mpipe_buffer_meta *out_meta;
	uint32_t in_used;
	uint32_t out_used;

	int ret;

	if (in_buf == NULL || out_buf == NULL || transform->out_pool == NULL ||
	    transform->out_pool->acquire_buffer == NULL) {
		return -EINVAL;
	}

	*out_buf = NULL;

	/* The server reads the payload by address: it must be in the element's memory */
	if (transform->in_pool != NULL) {
		ret = mpipe_buffer_move_to_pool(transform->in_pool, in_buf, &in_buf);
		if (ret != 0) {
			LOG_ERR("Failed to copy the input into the element's pool (%d)", ret);
			return ret;
		}
	}

	in_meta = mpipe_buffer_get_meta(in_buf);
	in_used = in_meta->bytes_used;

	if (transform->out_pool->acquire_buffer(transform->out_pool, out_buf) != 0 ||
	    *out_buf == NULL) {
		LOG_ERR("Failed to acquire an output buffer");
		net_buf_unref(in_buf);
		return -ENOMEM;
	}

	out_meta = mpipe_buffer_get_meta(*out_buf);
	out_used = out_meta->bytes_used;

	/*
	 * The server reads the input from the memory and writes the output to
	 * it: this core's cache must hold nothing newer than the memory of the
	 * first, and nothing at all of the second.
	 */
	(void)sys_cache_data_flush_range(in_buf->data, in_used);
	(void)sys_cache_data_invd_range((*out_buf)->data, (*out_buf)->size);

#if MPIPE_STAGE_STATS
	uint32_t t0 = k_cycle_get_32();
#endif

	ret = mpipe_rpc_channel_process(transform_client->channel, (uintptr_t)in_buf->data, in_used,
					(uintptr_t)(*out_buf)->data, (*out_buf)->size, &out_used);

#if MPIPE_STAGE_STATS
	if (mpipe_stage_stats_add(&client_stats, (out_used != 0U) ? 1U : 0U, in_used, in_used,
				  mpipe_stage_stats_elapsed_us(t0))) {
		LOG_INF("stats transform_client: %u out/s, in %u B/s, call %u/%u/%u us min/avg/max "
			"(%u ms)",
			mpipe_stage_stats_per_s(&client_stats, client_stats.count),
			mpipe_stage_stats_per_s(&client_stats, client_stats.bytes),
			client_stats.us_min, mpipe_stage_stats_us_avg(&client_stats),
			client_stats.us_max, client_stats.elapsed_ms);
		mpipe_stage_stats_reset(&client_stats);
	}
#endif
	if (ret != 0) {
		LOG_ERR("Failed to process buffer via RPC");
		net_buf_unref(*out_buf);
		*out_buf = NULL;
		net_buf_unref(in_buf);

		return -EIO;
	}

	/* The server dropped this input: nothing goes on downstream */
	if (out_used == 0U) {
		net_buf_unref(*out_buf);
		*out_buf = NULL;
		net_buf_unref(in_buf);

		return 0;
	}

	out_meta->bytes_used = out_used;
	out_meta->pts = in_meta->pts;
	(*out_buf)->len = out_used;

	net_buf_unref(in_buf);

	return 0;
}

static int mpipe_transform_client_event_fn(struct mpipe_pad *pad, struct mpipe_dispatch *event)
{
	struct mpipe_transform_client *transform_client =
		(struct mpipe_transform_client *)pad->object.container;
	int ret;

	/* The server flushes first, so its last buffer precedes the event downstream */
	if (event->type == MPIPE_DISPATCH_EOS) {
		ret = mpipe_rpc_channel_eos(transform_client->channel);
		if (ret != 0) {
			LOG_ERR("The server failed to end the stream (%d)", ret);
			return ret;
		}
	}

	return transform_client->base_event_fn(pad, event);
}

static int mpipe_transform_client_change_state(struct mpipe_element *self,
					       enum mpipe_state_change transition)
{
	struct mpipe_transform_client *transform_client = (struct mpipe_transform_client *)self;
	struct mpipe_transform *transform = &transform_client->transform;
	int ret;

	/* READY to PAUSED is the server's own business, see mpipe_transform_client_init() */
	if (transition != MPIPE_STATE_CHANGE_READY_TO_PAUSED) {
		ret = mpipe_rpc_channel_set_state(transform_client->channel, transition);
		if (ret != 0) {
			LOG_ERR("The server refused the state change (%d)", ret);
			return ret;
		}
	}

	/* The pool is the element's to stop when nothing upstream adopted it */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY && transform->in_pool != NULL) {
		(void)mpipe_buffer_pool_stop(transform->in_pool);
	}

	return mpipe_transform_change_state(self, transition);
}

static int mpipe_transform_client_propose_buffer_pool(struct mpipe_transform *self,
						      struct mpipe_dispatch *query)
{
	query->pool = self->in_pool;

	return 0;
}

static int mpipe_transform_client_decide_buffer_pool(struct mpipe_transform *self,
						     struct mpipe_dispatch *query)
{
	struct mpipe_buffer_pool *query_pool = query->pool;
	struct mpipe_buffer_pool_config *pool_config = &self->out_pool->config;
	struct mpipe_buffer_pool_config *qpc = NULL;

	if (query_pool == NULL) {
		qpc = &query->pool_cfg;
	} else {
		qpc = &query_pool->config;
	}

	/* Always use its own pool, just negotiate the configs */
	if (qpc != NULL) {
		/* What downstream holds comes on top of what the server needs */
		pool_config->min_buffers = (uint8_t)MIN(
			(uint32_t)self->out_pool->req_config.min_buffers + qpc->min_buffers,
			UINT8_MAX);

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

int mpipe_transform_client_init(struct mpipe_transform_client *transform_client, uint8_t id)
{
	__ASSERT_NO_MSG(transform_client != NULL);

	struct mpipe_element *self = &transform_client->transform.element;
	struct mpipe_transform *transform = &transform_client->transform;
	int ret;

	__ASSERT_NO_MSG(transform_client->channel != NULL);

	ret = mpipe_rpc_channel_open(transform_client->channel);
	if (ret != 0) {
		LOG_ERR("Failed to reach the remote transform (%d)", ret);
		return ret;
	}

	ret = mpipe_transform_init(transform, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "transform_client");

	/* Only NORMAL mode is supported */
	transform->mode = MPIPE_TRANSFORM_MODE_NORMAL;

	self->change_state = mpipe_transform_client_change_state;

	transform_client->base_event_fn = transform->sink_pad.event_fn;
	transform->sink_pad.event_fn = mpipe_transform_client_event_fn;
	transform->sink_pad.process_fn = mpipe_transform_client_process_fn;
	transform->decide_buffer_pool = mpipe_transform_client_decide_buffer_pool;
	transform->propose_buffer_pool = mpipe_transform_client_propose_buffer_pool;

	return 0;
}
