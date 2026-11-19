/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_sink_client.h>

LOG_MODULE_REGISTER(mpipe_sink_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_sink_client_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					struct net_buf **out_buf)
{
	struct mpipe_sink_client *sink_client = (struct mpipe_sink_client *)pad->object.container;
	int ret;

	*out_buf = NULL;

	/* The server reads the payload by address: it must be in the element's memory */
	if (sink_client->in_pool != NULL) {
		ret = mpipe_buffer_move_to_pool(sink_client->in_pool, in_buf, &in_buf);
		if (ret != 0) {
			LOG_ERR("Failed to copy the input into the element's pool (%d)", ret);
			return ret;
		}
	}

	ret = mpipe_rpc_channel_process(sink_client->channel, (uintptr_t)in_buf->data,
					mpipe_buffer_get_meta(in_buf)->bytes_used, 0U, 0U, NULL);
	net_buf_unref(in_buf);
	if (ret != 0) {
		LOG_ERR("Failed to process buffer via RPC (%d)", ret);
		return -EIO;
	}

	return 0;
}

static int mpipe_sink_client_event_fn(struct mpipe_pad *pad, struct mpipe_dispatch *event)
{
	struct mpipe_sink_client *sink_client = (struct mpipe_sink_client *)pad->object.container;
	int ret;

	/* The end of stream is posted once the remote consumer has seen it */
	if (event->type == MPIPE_DISPATCH_EOS) {
		ret = mpipe_rpc_channel_eos(sink_client->channel);
		if (ret != 0) {
			LOG_ERR("The server failed to end the stream (%d)", ret);
			return ret;
		}
	}

	return sink_client->base_event_fn(pad, event);
}

static int mpipe_sink_client_propose_buffer_pool(struct mpipe_sink *self,
						 struct mpipe_dispatch *query)
{
	struct mpipe_sink_client *sink_client = (struct mpipe_sink_client *)self;

	query->pool = sink_client->in_pool;

	return 0;
}

static int mpipe_sink_client_change_state(struct mpipe_element *self,
					  enum mpipe_state_change transition)
{
	struct mpipe_sink_client *sink_client = (struct mpipe_sink_client *)self;
	int ret;

	/* READY to PAUSED is the server's own business, see mpipe_sink_client_init() */
	if (transition != MPIPE_STATE_CHANGE_READY_TO_PAUSED) {
		ret = mpipe_rpc_channel_set_state(sink_client->channel, transition);
		if (ret != 0) {
			LOG_ERR("The server refused the state change (%d)", ret);
			return ret;
		}
	}

	/* The pool is the element's to stop when nothing upstream adopted it */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY && sink_client->in_pool != NULL) {
		(void)mpipe_buffer_pool_stop(sink_client->in_pool);
	}

	return mpipe_sink_change_state(self, transition);
}

int mpipe_sink_client_init(struct mpipe_sink_client *sink_client, uint8_t id)
{
	__ASSERT_NO_MSG(sink_client != NULL);

	struct mpipe_element *self = &sink_client->sink.element;
	struct mpipe_sink *sink = &sink_client->sink;
	int ret;

	__ASSERT_NO_MSG(sink_client->channel != NULL);

	ret = mpipe_rpc_channel_open(sink_client->channel);
	if (ret != 0) {
		LOG_ERR("Failed to reach the remote sink (%d)", ret);
		return ret;
	}

	ret = mpipe_sink_init(sink, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "sink_client");

	self->change_state = mpipe_sink_client_change_state;

	sink_client->base_event_fn = sink->sink_pad.event_fn;
	sink->sink_pad.event_fn = mpipe_sink_client_event_fn;
	sink->sink_pad.process_fn = mpipe_sink_client_process_fn;
	sink->propose_buffer_pool = mpipe_sink_client_propose_buffer_pool;

	return 0;
}
