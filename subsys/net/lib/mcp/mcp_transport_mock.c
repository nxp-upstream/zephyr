/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <string.h>

#include "mcp_common.h"
#include "mcp_server_internal.h"

LOG_MODULE_REGISTER(mcp_transport_mock, CONFIG_MCP_LOG_LEVEL);

struct mock_client_context {
	uint32_t client_id;
	bool active;
	char last_message[CONFIG_MCP_MAX_MESSAGE_SIZE];
	size_t last_message_len;
};

struct mock_transport_context {
	struct mock_client_context clients[CONFIG_MCP_MOCK_MAX_CLIENTS];
	int send_call_count;
	int disconnect_call_count;
	uint32_t last_client_id;
	int inject_send_error;
	int inject_disconnect_error;
};

static struct mock_transport_context mock_ctx;

static int mock_transport_send(struct mcp_transport_binding *binding, uint32_t client_id,
				const void *data, size_t length)
{
	ARG_UNUSED(binding);

	if (mock_ctx.inject_send_error != 0) {
		LOG_DBG("Mock: Injecting send error %d", mock_ctx.inject_send_error);
		return mock_ctx.inject_send_error;
	}

	mock_ctx.send_call_count++;
	mock_ctx.last_client_id = client_id;

	for (int i = 0; i < ARRAY_SIZE(mock_ctx.clients); i++) {
		if (mock_ctx.clients[i].client_id == client_id && mock_ctx.clients[i].active) {
			size_t copy_len = MIN(length, sizeof(mock_ctx.clients[i].last_message) - 1);

			memcpy(mock_ctx.clients[i].last_message, data, copy_len);
			mock_ctx.clients[i].last_message[copy_len] = '\0';
			mock_ctx.clients[i].last_message_len = copy_len;

			LOG_DBG("Mock: Sent %zu bytes to client %u", length, client_id);
			return 0;
		}
	}

	LOG_ERR("Mock: Client %u not found", client_id);
	return -ENOENT;
}

static int mock_transport_disconnect(struct mcp_transport_binding *binding, uint32_t client_id)
{
	ARG_UNUSED(binding);

	if (mock_ctx.inject_disconnect_error != 0) {
		LOG_DBG("Mock: Injecting disconnect error %d", mock_ctx.inject_disconnect_error);
		return mock_ctx.inject_disconnect_error;
	}

	mock_ctx.disconnect_call_count++;

	for (int i = 0; i < ARRAY_SIZE(mock_ctx.clients); i++) {
		if (mock_ctx.clients[i].client_id == client_id) {
			mock_ctx.clients[i].active = false;
			LOG_DBG("Mock: Disconnected client %u", client_id);
			return 0;
		}
	}

	LOG_WRN("Mock: Client %u not found for disconnect", client_id);
	return -ENOENT;
}

static const struct mcp_transport_ops mock_ops = {
	.send = mock_transport_send,
	.disconnect = mock_transport_disconnect,
};

void mcp_transport_mock_new_client_callback(struct mcp_transport_binding *binding,
					    uint32_t client_id)
{
	if (binding == NULL) {
		LOG_ERR("Mock: NULL binding in new client callback");
		return;
	}

	binding->ops = &mock_ops;
	binding->context = &mock_ctx;

	for (int i = 0; i < ARRAY_SIZE(mock_ctx.clients); i++) {
		if (!mock_ctx.clients[i].active) {
			mock_ctx.clients[i].client_id = client_id;
			mock_ctx.clients[i].active = true;
			mock_ctx.clients[i].last_message_len = 0;
			memset(mock_ctx.clients[i].last_message, 0,
			       sizeof(mock_ctx.clients[i].last_message));
			LOG_DBG("Mock: Registered client %u in slot %d", client_id, i);
			return;
		}
	}

	LOG_ERR("Mock: No available client slots");
}

void mcp_transport_mock_inject_send_error(int error)
{
	mock_ctx.inject_send_error = error;
	LOG_DBG("Mock: Will inject send error %d", error);
}

void mcp_transport_mock_inject_disconnect_error(int error)
{
	mock_ctx.inject_disconnect_error = error;
	LOG_DBG("Mock: Will inject disconnect error %d", error);
}

int mcp_transport_mock_get_send_count(void)
{
	return mock_ctx.send_call_count;
}

void mcp_transport_mock_reset_send_count(void)
{
	mock_ctx.send_call_count = 0;
}

int mcp_transport_mock_get_disconnect_count(void)
{
	return mock_ctx.disconnect_call_count;
}

uint32_t mcp_transport_mock_get_last_client_id(void)
{
	return mock_ctx.last_client_id;
}

const char *mcp_transport_mock_get_last_message(uint32_t client_id, size_t *length)
{
	for (int i = 0; i < ARRAY_SIZE(mock_ctx.clients); i++) {
		if (mock_ctx.clients[i].client_id == client_id && mock_ctx.clients[i].active) {
			if (length) {
				*length = mock_ctx.clients[i].last_message_len;
			}
			return mock_ctx.clients[i].last_message;
		}
	}

	if (length) {
		*length = 0;
	}
	return NULL;
}