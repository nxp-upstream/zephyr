/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_src_client.h>

LOG_MODULE_REGISTER(mpipe_src_client, CONFIG_MPIPE_LOG_LEVEL);

int mpipe_src_client_fill(struct mpipe_src_client *src_client, struct net_buf *buf)
{
	struct mpipe_buffer_meta *meta = mpipe_buffer_get_meta(buf);
	uint32_t used = 0;
	int ret;

	/* No input: the server gives the next buffer of its own source */
	ret = mpipe_rpc_channel_process(src_client->channel, 0U, 0U, (uintptr_t)buf->data,
					buf->size, &used);
	if (ret == -EAGAIN || ret == -ENODATA) {
		return ret;
	}

	if (ret != 0 || used == 0U || used > buf->size) {
		LOG_ERR("Failed to get a buffer from the server (%d)", ret);
		return -EIO;
	}

	meta->bytes_used = used;
	meta->pts = 0;
	buf->len = used;

	return 0;
}

static int mpipe_src_client_change_state(struct mpipe_element *self,
					 enum mpipe_state_change transition)
{
	struct mpipe_src_client *src_client = (struct mpipe_src_client *)self;
	int ret;

	/* READY to PAUSED is the server's own business, see mpipe_src_client_init() */
	if (transition != MPIPE_STATE_CHANGE_READY_TO_PAUSED) {
		ret = mpipe_rpc_channel_set_state(src_client->channel, transition);
		if (ret != 0) {
			LOG_ERR("The server refused the state change (%d)", ret);
			return ret;
		}
	}

	return mpipe_src_change_state(self, transition);
}

/* The element keeps its own pool: only the configuration is negotiated */
static int mpipe_src_client_decide_buffer_pool(struct mpipe_src *self, struct mpipe_dispatch *query)
{
	const struct mpipe_buffer_pool_config *asked =
		(query->pool != NULL) ? &query->pool->config : &query->pool_cfg;
	struct mpipe_buffer_pool_config *config = &self->pool->config;
	int align;

	/* What downstream holds comes on top of what the server needs */
	config->min_buffers = (uint8_t)MIN(
		(uint32_t)self->pool->req_config.min_buffers + asked->min_buffers, UINT8_MAX);

	align = sys_lcm(asked->align, config->align);
	if (align == -1) {
		return -EINVAL;
	}

	if (align != 0) {
		config->align = (uint16_t)align;
	} else if (asked->align != 0U) {
		config->align = asked->align;
	} else {
		/* Neither side asks for an alignment */
	}

	return 0;
}

int mpipe_src_client_init(struct mpipe_src_client *src_client, uint8_t id)
{
	__ASSERT_NO_MSG(src_client != NULL);
	__ASSERT_NO_MSG(src_client->channel != NULL);

	struct mpipe_element *self = &src_client->src.element;
	int ret;

	ret = mpipe_rpc_channel_open(src_client->channel);
	if (ret != 0) {
		LOG_ERR("Failed to reach the remote source (%d)", ret);
		return ret;
	}

	ret = mpipe_src_init(&src_client->src, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "src_client");

	self->change_state = mpipe_src_client_change_state;
	src_client->src.decide_buffer_pool = mpipe_src_client_decide_buffer_pool;

	return 0;
}
