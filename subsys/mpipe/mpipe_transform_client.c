/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_transform_client.h>

LOG_MODULE_REGISTER(mpipe_transform_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_transform_client_chain_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					   struct net_buf **out_buf)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)pad->object.container;
	struct mpipe_transform_client *transform_client =
		(struct mpipe_transform_client *)transform;
	struct mpipe_buffer_meta *in_meta;
	struct mpipe_buffer_meta *out_meta;
	uint32_t in_used;
	uint32_t out_used;

	if (in_buf == NULL || out_buf == NULL || transform->outpool == NULL ||
	    transform->outpool->acquire_buffer == NULL) {
		return -EINVAL;
	}

	in_meta = mpipe_buffer_get_meta(in_buf);
	in_used = in_meta ? in_meta->bytes_used : in_buf->len;

	if (transform->outpool->acquire_buffer(transform->outpool, out_buf) != 0 ||
	    *out_buf == NULL) {
		LOG_ERR("Failed to acquire an output buffer");
		return -ENOMEM;
	}

	out_meta = mpipe_buffer_get_meta(*out_buf);
	out_used = out_meta ? out_meta->bytes_used : (*out_buf)->len;

	/*
	 * RPC interface uses 32-bit addresses (remote MCU).
	 * Cast through uintptr_t to avoid pointer truncation warnings.
	 */
	if (transform_client->chain_fn_rpc((uint32_t)(uintptr_t)in_buf->data, in_used,
					   (uint32_t)(uintptr_t)(*out_buf)->data, &out_used) != 0) {
		LOG_ERR("Failed to process buffer via RPC");
		net_buf_unref(*out_buf);
		*out_buf = NULL;
		net_buf_unref(in_buf);

		return -EIO;
	}

	if (out_meta != NULL) {
		out_meta->bytes_used = out_used;
		out_meta->timestamp = k_uptime_get_32();
	}
	(*out_buf)->len = out_used;

	net_buf_unref(in_buf);

	return 0;
}

static int mpipe_transform_client_propose_allocation(struct mpipe_transform *self,
						     struct mpipe_dispatch *query)
{
	return mpipe_dispatch_set_pool(query, self->inpool);
}

static int mpipe_transform_client_decide_allocation(struct mpipe_transform *self,
						    struct mpipe_dispatch *query)
{
	struct mpipe_buffer_pool *query_pool = mpipe_dispatch_get_pool(query);
	struct mpipe_buffer_pool_config *pool_config = &self->outpool->config;
	struct mpipe_buffer_pool_config *qpc = NULL;

	if (query_pool == NULL) {
		qpc = mpipe_dispatch_get_pool_config(query);
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

void mpipe_transform_client_init(struct mpipe_element *self)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)self;
	struct mpipe_transform_client *transform_client =
		(struct mpipe_transform_client *)transform;

	(void)transform_client->init_rpc();

	/* Init base class */
	mpipe_transform_init(self);

	/* Support only normal mode for now */
	transform->mode = MPIPE_MODE_NORMAL;

	transform->sink_pad.chain_fn = mpipe_transform_client_chain_fn;
	transform->decide_allocation = mpipe_transform_client_decide_allocation;
	transform->propose_allocation = mpipe_transform_client_propose_allocation;
}
