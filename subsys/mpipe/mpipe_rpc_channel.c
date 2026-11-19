/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/mpipe/mpipe_rpc_channel.h>

void mpipe_rpc_channel_init(struct mpipe_rpc_channel *channel,
			    const struct mpipe_rpc_channel_api *api)
{
	__ASSERT_NO_MSG(channel != NULL);
	__ASSERT_NO_MSG(api != NULL && api->open != NULL && api->process != NULL);

	channel->api = api;
	k_mutex_init(&channel->lock);
}

int mpipe_rpc_channel_open(struct mpipe_rpc_channel *channel)
{
	return channel->api->open(channel);
}

int mpipe_rpc_channel_process(struct mpipe_rpc_channel *channel, uintptr_t in_buf, uint32_t in_sz,
			      uintptr_t out_buf, uint32_t out_cap, uint32_t *out_sz)
{
	int ret;

	(void)k_mutex_lock(&channel->lock, K_FOREVER);
	ret = channel->api->process(channel, in_buf, in_sz, out_buf, out_cap, out_sz);
	(void)k_mutex_unlock(&channel->lock);

	return ret;
}

int mpipe_rpc_channel_set_state(struct mpipe_rpc_channel *channel,
				enum mpipe_state_change transition)
{
	int ret;

	if (channel->api->set_state == NULL) {
		return 0;
	}

	(void)k_mutex_lock(&channel->lock, K_FOREVER);
	ret = channel->api->set_state(channel, transition);
	(void)k_mutex_unlock(&channel->lock);

	return ret;
}

int mpipe_rpc_channel_eos(struct mpipe_rpc_channel *channel)
{
	int ret;

	if (channel->api->eos == NULL) {
		return 0;
	}

	(void)k_mutex_lock(&channel->lock, K_FOREVER);
	ret = channel->api->eos(channel);
	(void)k_mutex_unlock(&channel->lock);

	return ret;
}
