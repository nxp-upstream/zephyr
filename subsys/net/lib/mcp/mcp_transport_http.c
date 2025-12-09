/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/socket.h>
#include "mcp_transport.h"
#include "mcp_json.h"

LOG_MODULE_REGISTER(mcp_transport_http, CONFIG_MCP_LOG_LEVEL);

#define MCP_SSE_MAX_CLIENTS CONFIG_HTTP_SERVER_MAX_CLIENTS

/* SSE client context */
struct sse_client {
	uint32_t client_id;
	int fd;
	bool connected;
	struct k_work_delayable keepalive_work;
	struct k_mutex send_mutex;
};

/* HTTP transport state */
static struct {
	struct sse_client clients[MCP_SSE_MAX_CLIENTS];
	struct k_mutex clients_mutex;
	uint32_t next_client_id;
	bool initialized;
	bool started;
} http_transport_state;

/* Forward declarations */
static int http_transport_init(void);
static int http_transport_start(void);
static int http_transport_stop(void);
static int http_transport_send(uint32_t client_id, const void *data, size_t length);
static bool http_transport_is_connected(uint32_t client_id);
static const char *http_transport_get_name(void);

/* Transport operations */
static const struct mcp_transport_ops http_transport_ops = {
	.init = http_transport_init,
	.start = http_transport_start,
	.stop = http_transport_stop,
	.send = http_transport_send,
	.is_connected = http_transport_is_connected,
	.get_name = http_transport_get_name,
};

/* Transport mechanism registration */
static const struct mcp_transport_mechanism http_transport_mechanism = {
	.name = "http-sse",
	.ops = &http_transport_ops,
};

/* Helper functions */

static struct sse_client *find_client_by_id(uint32_t client_id)
{
	for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
		if (http_transport_state.clients[i].client_id == client_id &&
			http_transport_state.clients[i].connected) {
			return &http_transport_state.clients[i];
		}
	}
	return NULL;
}

static struct sse_client *find_client_by_fd(int fd)
{
	for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
		if (http_transport_state.clients[i].fd == fd &&
			http_transport_state.clients[i].connected) {
			return &http_transport_state.clients[i];
		}
	}
	return NULL;
}

static struct sse_client *allocate_client(int fd)
{
	struct sse_client *client = NULL;
	int ret;

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return NULL;
	}

	for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
		if (!http_transport_state.clients[i].connected) {
			client = &http_transport_state.clients[i];
			client->client_id = ++http_transport_state.next_client_id;
			client->fd = fd;
			client->connected = true;
			break;
		}
	}

	k_mutex_unlock(&http_transport_state.clients_mutex);

	if (!client) {
		LOG_ERR("No available client slots");
	}

	return client;
}

static void release_client(struct sse_client *client)
{
	int ret;

	if (!client) {
		return;
	}

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return;
	}

	/* Cancel keepalive work */
	k_work_cancel_delayable(&client->keepalive_work);

	/* Notify transport layer of disconnection */
	mcp_transport_client_disconnected(client->client_id);

	client->connected = false;
	client->client_id = 0;
	client->fd = -1;

	k_mutex_unlock(&http_transport_state.clients_mutex);
}

/* SSE keepalive handler */
static void sse_keepalive_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct sse_client *client = CONTAINER_OF(dwork, struct sse_client, keepalive_work);
	const char *keepalive_msg = ": keepalive\n\n";
	int ret;

	if (!client->connected) {
		return;
	}

	ret = k_mutex_lock(&client->send_mutex, K_MSEC(100));
	if (ret != 0) {
		LOG_WRN("Failed to lock send mutex for keepalive");
		goto reschedule;
	}

	ret = send(client->fd, keepalive_msg, strlen(keepalive_msg), 0);
	k_mutex_unlock(&client->send_mutex);

	if (ret < 0) {
		LOG_ERR("Failed to send keepalive to client %u: %d", client->client_id, errno);
		release_client(client);
		return;
	}

reschedule:
	/* Reschedule keepalive */
	k_work_reschedule(&client->keepalive_work, K_MSEC(CONFIG_MCP_HTTP_SSE_KEEPALIVE_MS));
}

/* Send SSE event */
static int send_sse_event(struct sse_client *client, const char *event_type,
			  const char *data, size_t data_len)
{
	char header[128];
	int ret;
	int header_len;

	if (!client || !client->connected) {
		return -ENOTCONN;
	}

	ret = k_mutex_lock(&client->send_mutex, K_MSEC(1000));
	if (ret != 0) {
		LOG_ERR("Failed to lock send mutex: %d", ret);
		return ret;
	}

	/* Send event type if specified */
	if (event_type) {
		header_len = snprintf(header, sizeof(header), "event: %s\n", event_type);
		ret = send(client->fd, header, header_len, 0);
		if (ret < 0) {
			LOG_ERR("Failed to send event type: %d", errno);
			k_mutex_unlock(&client->send_mutex);
			return -errno;
		}
	}

	/* Send data field */
	header_len = snprintf(header, sizeof(header), "data: ");
	ret = send(client->fd, header, header_len, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send data header: %d", errno);
		k_mutex_unlock(&client->send_mutex);
		return -errno;
	}

	/* Send actual data */
	ret = send(client->fd, data, data_len, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send data: %d", errno);
		k_mutex_unlock(&client->send_mutex);
		return -errno;
	}

	/* Send double newline to end event */
	ret = send(client->fd, "\n\n", 2, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send event terminator: %d", errno);
		k_mutex_unlock(&client->send_mutex);
		return -errno;
	}

	k_mutex_unlock(&client->send_mutex);
	return 0;
}

/* HTTP endpoint handlers */

static int sse_endpoint_handler(struct http_client_ctx *client,
				enum http_data_status status,
				const struct http_request_ctx *request_ctx,
				struct http_response_ctx *response_ctx,
				void *user_data)
{
	struct sse_client *sse_client;
	int ret;

	LOG_DBG("SSE endpoint handler called, status: %d", status);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		LOG_INF("SSE connection aborted");
		sse_client = find_client_by_fd(client->fd);
		if (sse_client) {
			release_client(sse_client);
		}
		return 0;
	}

	/* Only handle initial request */
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	/* Allocate SSE client */
	sse_client = allocate_client(client->fd);
	if (!sse_client) {
		LOG_ERR("Failed to allocate SSE client");
		response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
		return 0;
	}

	LOG_INF("SSE client %u connected on fd %d", sse_client->client_id, client->fd);

	/* Set up SSE response headers */
	static const struct http_header sse_headers[] = {
		{.name = "Content-Type", .value = "text/event-stream"},
		{.name = "Cache-Control", .value = "no-cache"},
		{.name = "Connection", .value = "keep-alive"},
		{.name = "X-Accel-Buffering", .value = "no"},
	};

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = sse_headers;
	response_ctx->header_count = ARRAY_SIZE(sse_headers);
	response_ctx->body = NULL;
	response_ctx->body_len = 0;
	response_ctx->final_chunk = false; /* Keep connection open */

	/* Initialize keepalive work */
	k_work_init_delayable(&sse_client->keepalive_work, sse_keepalive_handler);
	ret = k_mutex_init(&sse_client->send_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init send mutex: %d", ret);
		release_client(sse_client);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		return 0;
	}

	/* Schedule first keepalive */
	k_work_reschedule(&sse_client->keepalive_work, K_MSEC(CONFIG_MCP_HTTP_SSE_KEEPALIVE_MS));

	/* Notify transport layer of new connection */
	mcp_transport_client_connected(sse_client->client_id);

	/* Send initial connection event */
	const char *welcome_msg = "{\"type\":\"connection\",\"status\":\"established\"}";
	send_sse_event(sse_client, "message", welcome_msg, strlen(welcome_msg));

	return 0;
}

static int message_endpoint_handler(struct http_client_ctx *client,
					enum http_data_status status,
					const struct http_request_ctx *request_ctx,
					struct http_response_ctx *response_ctx,
					void *user_data)
{
	struct sse_client *sse_client;
	int ret;

	LOG_DBG("Message endpoint handler called, status: %d", status);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		LOG_WRN("Message request aborted");
		return 0;
	}

	/* Wait for complete request */
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	/* Find SSE client by file descriptor to get client_id */
	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_MSEC(100));
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		return 0;
	}

	sse_client = find_client_by_fd(client->fd);
	if (!sse_client) {
		k_mutex_unlock(&http_transport_state.clients_mutex);
		LOG_ERR("No SSE client found for this connection");
		response_ctx->status = HTTP_403_FORBIDDEN;
		static const char *error_msg = "{\"error\":\"No active SSE connection\"}";
		response_ctx->body = (const uint8_t *)error_msg;
		response_ctx->body_len = strlen(error_msg);
		response_ctx->final_chunk = true;
		return 0;
	}

	uint32_t client_id = sse_client->client_id;
	k_mutex_unlock(&http_transport_state.clients_mutex);

	/* Verify request has data */
	if (request_ctx->data_len == 0) {
		LOG_ERR("Empty message received");
		response_ctx->status = HTTP_400_BAD_REQUEST;
		static const char *error_msg = "{\"error\":\"Empty request body\"}";
		response_ctx->body = (const uint8_t *)error_msg;
		response_ctx->body_len = strlen(error_msg);
		response_ctx->final_chunk = true;
		return 0;
	}

	/* Submit request to transport layer - it handles JSON parsing and forwarding */
	ret = mcp_transport_send_request((const char *)request_ctx->data,
					 request_ctx->data_len, client_id);
	if (ret != 0) {
		LOG_ERR("Failed to submit request: %d", ret);

		/* Determine appropriate error response */
		if (ret == -EINVAL) {
			response_ctx->status = HTTP_400_BAD_REQUEST;
			static const char *error_msg = "{\"error\":\"Invalid JSON request\"}";
			response_ctx->body = (const uint8_t *)error_msg;
			response_ctx->body_len = strlen(error_msg);
		} else {
			response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
			static const char *error_msg = "{\"error\":\"Server busy\"}";
			response_ctx->body = (const uint8_t *)error_msg;
			response_ctx->body_len = strlen(error_msg);
		}
		response_ctx->final_chunk = true;
		return 0;
	}

	/* Send acknowledgment */
	response_ctx->status = HTTP_202_ACCEPTED;
	static const struct http_header json_header = {
		.name = "Content-Type",
		.value = "application/json"
	};
	response_ctx->headers = &json_header;
	response_ctx->header_count = 1;
	static const char *ack_msg = "{\"status\":\"accepted\"}";
	response_ctx->body = (const uint8_t *)ack_msg;
	response_ctx->body_len = strlen(ack_msg);
	response_ctx->final_chunk = true;

	return 0;
}

/* ============================================================================
 * HTTP Service and Resource Definitions
 * ============================================================================ */

static uint16_t mcp_http_port = CONFIG_MCP_HTTP_PORT;

static struct http_resource_detail_dynamic sse_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "text/event-stream",
		},
	.cb = sse_endpoint_handler,
	.user_data = NULL,
};

static struct http_resource_detail_dynamic message_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
			.content_type = "application/json",
		},
	.cb = message_endpoint_handler,
	.user_data = NULL,
};

/* Resources FIRST */
HTTP_RESOURCE_DEFINE(mcp_sse_resource, mcp_http_service, CONFIG_MCP_HTTP_SSE_ENDPOINT,
			 &sse_resource_detail);

HTTP_RESOURCE_DEFINE(mcp_message_resource, mcp_http_service, CONFIG_MCP_HTTP_MESSAGE_ENDPOINT,
			 &message_resource_detail);

/* Service LAST */
HTTP_SERVICE_DEFINE(mcp_http_service, NULL, &mcp_http_port,
			CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

/* Transport operations implementation */

static int http_transport_init(void)
{
	int ret;

	LOG_INF("Initializing HTTP/SSE transport");

	if (http_transport_state.initialized) {
		LOG_WRN("HTTP transport already initialized");
		return 0;
	}

	ret = k_mutex_init(&http_transport_state.clients_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init clients mutex: %d", ret);
		return ret;
	}

	/* Initialize client slots */
	memset(http_transport_state.clients, 0, sizeof(http_transport_state.clients));
	for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
		http_transport_state.clients[i].fd = -1;
		http_transport_state.clients[i].connected = false;
	}

	http_transport_state.next_client_id = 0;
	http_transport_state.initialized = true;

	LOG_INF("HTTP/SSE transport initialized");
	return 0;
}

static int http_transport_start(void)
{
	int ret;

	LOG_INF("Starting HTTP/SSE transport");

	if (!http_transport_state.initialized) {
		LOG_ERR("HTTP transport not initialized");
		return -EINVAL;
	}

	if (http_transport_state.started) {
		LOG_WRN("HTTP transport already started");
		return 0;
	}

	/* Start HTTP server */
	ret = http_server_start();
	if (ret != 0) {
		LOG_ERR("Failed to start HTTP server: %d", ret);
		return ret;
	}

	http_transport_state.started = true;

	LOG_INF("HTTP/SSE transport started on port 8080");
	LOG_INF("SSE endpoint: http://0.0.0.0:8080%s", CONFIG_MCP_HTTP_SSE_ENDPOINT);
	LOG_INF("Message endpoint: http://0.0.0.0:8080%s", CONFIG_MCP_HTTP_MESSAGE_ENDPOINT);

	return 0;
}

static int http_transport_stop(void)
{
	int ret;

	LOG_INF("Stopping HTTP/SSE transport");

	if (!http_transport_state.started) {
		return 0;
	}

	/* Disconnect all clients */
	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return ret;
	}

	for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
		if (http_transport_state.clients[i].connected) {
			release_client(&http_transport_state.clients[i]);
		}
	}

	k_mutex_unlock(&http_transport_state.clients_mutex);

	/* Stop HTTP server */
	ret = http_server_stop();
	if (ret != 0) {
		LOG_ERR("Failed to stop HTTP server: %d", ret);
		return ret;
	}

	http_transport_state.started = false;

	LOG_INF("HTTP/SSE transport stopped");
	return 0;
}

static int http_transport_send(uint32_t client_id, const void *data, size_t length)
{
	struct sse_client *client;
	int ret;

	if (!data || length == 0) {
		LOG_ERR("Invalid send parameters");
		return -EINVAL;
	}

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_MSEC(100));
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return ret;
	}

	/* If client_id is 0, broadcast to all connected clients */
	if (client_id == 0) {
		LOG_DBG("Broadcasting message to all clients");
		int sent_count = 0;

		for (int i = 0; i < MCP_SSE_MAX_CLIENTS; i++) {
			if (http_transport_state.clients[i].connected) {
				ret = send_sse_event(&http_transport_state.clients[i],
							"message", data, length);
				if (ret == 0) {
					sent_count++;
				} else {
					LOG_WRN("Failed to send to client %u: %d",
						http_transport_state.clients[i].client_id, ret);
				}
			}
		}

		k_mutex_unlock(&http_transport_state.clients_mutex);

		if (sent_count == 0) {
			LOG_WRN("No clients available for broadcast");
			return -ENOTCONN;
		}

		LOG_DBG("Broadcast sent to %d clients", sent_count);
		return 0;
	}

	/* Send to specific client */
	client = find_client_by_id(client_id);
	if (!client) {
		LOG_ERR("Client %u not found", client_id);
		k_mutex_unlock(&http_transport_state.clients_mutex);
		return -ENOENT;
	}

	k_mutex_unlock(&http_transport_state.clients_mutex);

	ret = send_sse_event(client, "message", data, length);
	if (ret != 0) {
		LOG_ERR("Failed to send to client %u: %d", client_id, ret);
		return ret;
	}

	LOG_DBG("Sent %zu bytes to client %u", length, client_id);
	return 0;
}

static bool http_transport_is_connected(uint32_t client_id)
{
	struct sse_client *client;
	bool connected;
	int ret;

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_MSEC(100));
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return false;
	}

	client = find_client_by_id(client_id);
	connected = (client != NULL && client->connected);

	k_mutex_unlock(&http_transport_state.clients_mutex);

	return connected;
}

static const char *http_transport_get_name(void)
{
	return "HTTP/SSE";
}

/* Auto-registration */
static int http_transport_auto_register(void)
{
	int ret;

	ret = mcp_transport_register_mechanism(&http_transport_mechanism);
	if (ret != 0) {
		LOG_ERR("Failed to register HTTP transport: %d", ret);
		return ret;
	}

	LOG_INF("HTTP/SSE transport mechanism registered");
	return 0;
}

SYS_INIT(http_transport_auto_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
