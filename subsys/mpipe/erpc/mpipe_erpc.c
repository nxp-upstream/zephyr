/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/erpc/mpipe_erpc.h>

#include "mpipe_erpc_objects.h"

LOG_MODULE_REGISTER(mpipe_erpc, CONFIG_MPIPE_LOG_LEVEL);

static const char *mpipe_erpc_status_str(erpc_status_t err)
{
	switch (err) {
	case kErpcStatus_Fail:
		return "generic failure";
	case kErpcStatus_InvalidArgument:
		return "invalid argument";
	case kErpcStatus_Timeout:
		return "timeout";
	case kErpcStatus_InvalidMessageVersion:
		return "unknown message version";
	case kErpcStatus_ExpectedReply:
		return "expected a reply, got another message type";
	case kErpcStatus_CrcCheckFailed:
		return "corrupted message";
	case kErpcStatus_BufferOverrun:
		return "buffer overrun";
	case kErpcStatus_UnknownName:
		return "unknown host name";
	case kErpcStatus_ConnectionFailure:
		return "connection failed";
	case kErpcStatus_ConnectionClosed:
		return "connection closed by peer";
	case kErpcStatus_MemoryError:
		return "memory allocation failed";
	case kErpcStatus_ServerIsDown:
		return "server is down";
	case kErpcStatus_InitFailed:
		return "transport initialization failed";
	case kErpcStatus_ReceiveFailed:
		return "receive failed";
	case kErpcStatus_SendFailed:
		return "send failed";
	default:
		return "unknown error";
	}
}

void mpipe_erpc_error_handler(erpc_status_t err, uint32_t function_id)
{
	if (err == kErpcStatus_Success) {
		return;
	}

	if (function_id != 0U) {
		LOG_ERR("eRPC error %d (%s) in function %u", (int)err, mpipe_erpc_status_str(err),
			function_id);
	} else {
		LOG_ERR("eRPC error %d (%s)", (int)err, mpipe_erpc_status_str(err));
	}
}

/* An endpoint and its eRPC client, shared by the channels that name the endpoint */
struct mpipe_erpc_endpoint {
	const char *name;
	erpc_client_t client;
	/* The client makes one call at a time */
	struct k_mutex lock;
};

struct mpipe_erpc_channel {
	struct mpipe_rpc_channel channel;
	struct mpipe_erpc_endpoint *endpoint;
	/* What the element runs on the server, and the instance the server opened for it */
	uint32_t function;
	uint32_t instance;
};

/* Protects the tables of channels and endpoints */
static K_MUTEX_DEFINE(mpipe_erpc_lock);

static inline struct mpipe_erpc_channel *mpipe_erpc_channel_of(struct mpipe_rpc_channel *channel)
{
	return CONTAINER_OF(channel, struct mpipe_erpc_channel, channel);
}

erpc_client_t mpipe_erpc_channel_lock(struct mpipe_rpc_channel *channel, uint32_t *instance)
{
	struct mpipe_erpc_channel *erpc_channel = mpipe_erpc_channel_of(channel);

	(void)k_mutex_lock(&erpc_channel->endpoint->lock, K_FOREVER);

	if (instance != NULL) {
		*instance = erpc_channel->instance;
	}

	return erpc_channel->endpoint->client;
}

void mpipe_erpc_channel_unlock(struct mpipe_rpc_channel *channel)
{
	(void)k_mutex_unlock(&mpipe_erpc_channel_of(channel)->endpoint->lock);
}

static int mpipe_erpc_channel_open(struct mpipe_rpc_channel *channel)
{
	struct mpipe_erpc_channel *erpc_channel = mpipe_erpc_channel_of(channel);
	struct mpipe_erpc_endpoint *endpoint = erpc_channel->endpoint;
	int ret;

	(void)k_mutex_lock(&endpoint->lock, K_FOREVER);

	/* The first channel of the endpoint connects it */
	if (endpoint->client == NULL) {
		endpoint->client = mpipe_erpc_objects_client_create(
			endpoint->name, CONFIG_MPIPE_ERPC_INIT_DELAY_MS, mpipe_erpc_error_handler);
	}

	if (endpoint->client == NULL) {
		LOG_ERR("Failed to bring up the eRPC client on %s", endpoint->name);
		ret = -EIO;
	} else {
		ret = mpipe_erpc_objects_stream_open(endpoint->client, erpc_channel->function,
						     &erpc_channel->instance);
		if (ret != 0) {
			LOG_ERR("The server on %s refused function %u (%d)", endpoint->name,
				(unsigned int)erpc_channel->function, ret);
		}
	}

	(void)k_mutex_unlock(&endpoint->lock);

	return ret;
}

static int mpipe_erpc_channel_process(struct mpipe_rpc_channel *channel, uintptr_t in_buf,
				      uint32_t in_sz, uintptr_t out_buf, uint32_t out_cap,
				      uint32_t *out_sz)
{
	/* A sink client expects no output */
	uint32_t unused;
	uint32_t instance;
	erpc_client_t client;
	int ret;

	/* The interface carries 32-bit addresses */
	if (in_buf > UINT32_MAX || out_buf > UINT32_MAX) {
		return -EFAULT;
	}

	client = mpipe_erpc_channel_lock(channel, &instance);
	ret = mpipe_erpc_objects_stream_process(client, instance, (uint32_t)in_buf, in_sz,
						(uint32_t)out_buf, out_cap,
						(out_sz != NULL) ? out_sz : &unused);
	mpipe_erpc_channel_unlock(channel);

	return ret;
}

static int mpipe_erpc_channel_set_state(struct mpipe_rpc_channel *channel,
					enum mpipe_state_change transition)
{
	uint32_t instance;
	erpc_client_t client;
	int ret;

	client = mpipe_erpc_channel_lock(channel, &instance);
	ret = mpipe_erpc_objects_stream_set_state(client, instance, (uint8_t)transition);
	mpipe_erpc_channel_unlock(channel);

	return ret;
}

static int mpipe_erpc_channel_eos(struct mpipe_rpc_channel *channel)
{
	uint32_t instance;
	erpc_client_t client;
	int ret;

	client = mpipe_erpc_channel_lock(channel, &instance);
	ret = mpipe_erpc_objects_stream_eos(client, instance);
	mpipe_erpc_channel_unlock(channel);

	return ret;
}

/* The endpoint of that name, taken from the table when no channel named it yet */
static struct mpipe_erpc_endpoint *mpipe_erpc_endpoint_get(const char *ept_name)
{
	static struct mpipe_erpc_endpoint endpoints[CONFIG_MPIPE_ERPC_ENDPOINTS_NUM];
	static uint8_t used;

	for (uint8_t i = 0; i < used; i++) {
		if (strcmp(endpoints[i].name, ept_name) == 0) {
			return &endpoints[i];
		}
	}

	if (used >= ARRAY_SIZE(endpoints)) {
		return NULL;
	}

	endpoints[used].name = ept_name;
	k_mutex_init(&endpoints[used].lock);
	used++;

	return &endpoints[used - 1U];
}

struct mpipe_rpc_channel *mpipe_erpc_channel_get(const char *ept_name, uint32_t function)
{
	static const struct mpipe_rpc_channel_api api = {
		.open = mpipe_erpc_channel_open,
		.process = mpipe_erpc_channel_process,
		.set_state = mpipe_erpc_channel_set_state,
		.eos = mpipe_erpc_channel_eos,
	};
	static struct mpipe_erpc_channel channels[CONFIG_MPIPE_ERPC_CHANNELS_NUM];
	static uint8_t used;
	struct mpipe_erpc_channel *erpc_channel = NULL;

	__ASSERT_NO_MSG(ept_name != NULL);

	(void)k_mutex_lock(&mpipe_erpc_lock, K_FOREVER);

	if (used >= ARRAY_SIZE(channels)) {
		LOG_ERR("No eRPC channel left for %s, see CONFIG_MPIPE_ERPC_CHANNELS_NUM",
			ept_name);
	} else {
		channels[used].endpoint = mpipe_erpc_endpoint_get(ept_name);
		if (channels[used].endpoint == NULL) {
			LOG_ERR("No eRPC endpoint left for %s, see CONFIG_MPIPE_ERPC_ENDPOINTS_NUM",
				ept_name);
		} else {
			erpc_channel = &channels[used];
			erpc_channel->function = function;
			mpipe_rpc_channel_init(&erpc_channel->channel, &api);
			used++;
		}
	}

	(void)k_mutex_unlock(&mpipe_erpc_lock);

	return (erpc_channel != NULL) ? &erpc_channel->channel : NULL;
}
