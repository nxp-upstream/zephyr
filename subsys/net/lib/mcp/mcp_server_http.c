/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/mcp/mcp_server_http.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

#include "mcp_common.h"
#include "mcp_json.h"
#include "mcp_server_internal.h"

LOG_MODULE_REGISTER(mcp_http_transport, CONFIG_MCP_LOG_LEVEL);

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define SESSION_ID_STR_LEN ((sizeof(uint32_t) * 2) + 1)
#define CONTENT_TYPE_HDR_LEN	\
	(sizeof("text/event-stream") + 1) /* worst case for content-type header */
#define ORIGIN_HDR_LEN	   128
#define MAX_RESPONSE_HEADERS 4 /* Content-Type, Last-Event-Id, Mcp-Session-Id, extra buffer */

/* Request accumulation buffer for each client */
struct mcp_http_request_accumulator {
	char data[CONFIG_MCP_MAX_MESSAGE_SIZE];
	size_t data_len;
	uint32_t session_id_hdr;
	uint32_t event_id_hdr;
	bool has_event_id;
	char content_type_hdr[CONTENT_TYPE_HDR_LEN];
	char origin_hdr[ORIGIN_HDR_LEN];
	uint32_t fd;
};

/* Response queue item structure */
struct mcp_http_response_item {
	void *fifo_reserved; /* Required for k_fifo */
	char *data;
	size_t length;
	uint32_t event_id;
};

/* HTTP Client context management */
struct mcp_http_client_ctx {
	uint32_t session_id;
	char session_id_str[SESSION_ID_STR_LEN];
	uint32_t next_event_id;
	struct http_header response_headers[MAX_RESPONSE_HEADERS];
	char response_body[CONFIG_MCP_MAX_MESSAGE_SIZE];
	struct k_fifo response_queue; /* Response queue for SSE */
	bool in_use;
};

/* HTTP transport state */
struct http_transport_state {
	struct mcp_http_request_accumulator accumulators[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex accumulators_mutex;
	struct mcp_http_client_ctx clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex clients_mutex;
	mcp_server_ctx_t *server_core;
	bool initialized;
};

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/
static struct mcp_http_request_accumulator *get_accumulator(int fd);
static int release_accumulator(struct mcp_http_request_accumulator *accumulator);
static int accumulate_request(struct http_client_ctx *client,
				  struct mcp_http_request_accumulator *accumulator,
				  const struct http_request_ctx *request_ctx,
				  enum http_data_status status);

static struct mcp_http_client_ctx *allocate_client(uint32_t session_id);
static int release_client(struct mcp_http_client_ctx *client);
int mcp_server_http_new_client_handler(struct mcp_transport_binding *binding, uint32_t client_id);

static int format_response(char *buffer, size_t buffer_size, const char *json_data);
static int format_sse_response(char *buffer, size_t buffer_size, int event_id, const char *data);
static int format_sse_retry_response(char *buffer, size_t buffer_size, uint32_t retry_ms);
static void cleanup_response_item(struct mcp_http_response_item *item);

static int mcp_endpoint_post_handler(struct http_client_ctx *client,
					 const struct http_request_ctx *request_ctx,
					 struct mcp_http_request_accumulator *accumulator,
					 struct http_response_ctx *response_ctx);
static int mcp_endpoint_get_handler(struct http_client_ctx *client,
					const struct http_request_ctx *request_ctx,
					struct mcp_http_request_accumulator *accumulator,
					struct http_response_ctx *response_ctx);
static int mcp_server_http_resource_handler(struct http_client_ctx *client,
						enum http_data_status status,
						const struct http_request_ctx *request_ctx,
						struct http_response_ctx *response_ctx,
						void *user_data);

static int mcp_server_http_send(struct mcp_transport_binding *binding, uint32_t client_id,
				const void *data, size_t length);
static int mcp_server_http_disconnect(struct mcp_transport_binding *binding, uint32_t client_id);

/*******************************************************************************
 * Static variables and resource definitions
 ******************************************************************************/
const struct mcp_transport_ops mcp_http_transport_ops = {
	.send = mcp_server_http_send,
	.disconnect = mcp_server_http_disconnect,
};

static struct http_transport_state http_transport_state;
static struct http_resource_detail_dynamic mcp_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST) | BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.cb = mcp_server_http_resource_handler,
	.user_data = NULL,
};

uint32_t mcp_http_port = CONFIG_MCP_HTTP_PORT;

/* HTTP resource definition */
HTTP_RESOURCE_DEFINE(mcp_endpoint_resource, mcp_http_service, CONFIG_MCP_HTTP_ENDPOINT,
			 &mcp_resource_detail);

/* HTTP service definition */
HTTP_SERVICE_DEFINE(mcp_http_service, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &mcp_http_port,
			1, CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL);

/* HTTP headers capture */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(origin_hdr, "Origin");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(content_type_hdr, "Content-Type");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(mcp_session_id_hdr, "Mcp-Session-Id");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(last_event_id_hdr, "Last-Event-Id");

/*******************************************************************************
 * Accumulator helpers
 ******************************************************************************/
/**
 * Get or allocate an accumulator for the given file descriptor
 * This function searches for an existing accumulator associated with the given fd.
 * If found, it returns a pointer to it. If not found, it allocates the first.
 */
static struct mcp_http_request_accumulator *get_accumulator(int fd)
{
	int ret;
	struct mcp_http_request_accumulator *first_available = NULL;
	struct mcp_http_request_accumulator *result = NULL;

	ret = k_mutex_lock(&http_transport_state.accumulators_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock accumulator mutex: %d", ret);
		return NULL;
	}

	for (int i = 0; i < ARRAY_SIZE(http_transport_state.accumulators); i++) {
		if (http_transport_state.accumulators[i].fd == fd) {
			result = &http_transport_state.accumulators[i];
			goto unlock;
		}

		if ((first_available == NULL) && (http_transport_state.accumulators[i].fd == -1)) {
			first_available = &http_transport_state.accumulators[i];
		}
	}

	if (first_available != NULL) {
		first_available->fd = fd;
		first_available->data_len = 0;
		first_available->has_event_id = false;
		result = first_available;
	}

unlock:
	k_mutex_unlock(&http_transport_state.accumulators_mutex);
	return result;
}


static int release_accumulator(struct mcp_http_request_accumulator *accumulator)
{
	int ret;

	if (accumulator == NULL) {
		return -EINVAL;
	}

	ret = k_mutex_lock(&http_transport_state.accumulators_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock accumulator mutex: %d", ret);
		return ret;
	}

	accumulator->data_len = 0;
	accumulator->fd = -1;

	k_mutex_unlock(&http_transport_state.accumulators_mutex);

	return 0;
}

static int accumulate_request(struct http_client_ctx *client,
				  struct mcp_http_request_accumulator *accumulator,
				  const struct http_request_ctx *request_ctx,
				  enum http_data_status status)
{
	char *endptr;

	/* Accumulate request data */
	if (request_ctx->data_len > 0) {
		if ((accumulator->data_len + request_ctx->data_len) > CONFIG_MCP_MAX_MESSAGE_SIZE) {
			LOG_WRN("Accumulator full. Dropping current chunk");
			return -ENOMEM;
		}

		memcpy(accumulator->data + accumulator->data_len, request_ctx->data,
			   request_ctx->data_len);
		accumulator->data_len += request_ctx->data_len;
	}

	/* Parse headers whenever they are available */
	if (request_ctx->header_count > 0) {
		for (uint32_t i = 0; i < request_ctx->header_count; i++) {
			const struct http_header *header = &request_ctx->headers[i];

			if (!header->name || !header->value) {
				continue;
			}

			if (strcmp(header->name, "Mcp-Session-Id") == 0) {
				accumulator->session_id_hdr = strtoul(header->value, &endptr, 16);
				if (*endptr != '\0') {
					LOG_ERR("Invalid Mcp-Session-Id format: %s", header->value);
					return -EINVAL;
				}
			} else if (strcmp(header->name, "Last-Event-Id") == 0) {
				accumulator->event_id_hdr = strtoul(header->value, &endptr, 10);
				if (*endptr != '\0') {
					LOG_ERR("Invalid Last-Event-Id format: %s", header->value);
					return -EINVAL;
				}
				accumulator->has_event_id = true;
			} else if (strcmp(header->name, "Origin") == 0) {
				strncpy(accumulator->origin_hdr, header->value,
					sizeof(accumulator->origin_hdr) - 1);
				accumulator->origin_hdr[sizeof(accumulator->origin_hdr) - 1] = '\0';
			} else if (strcmp(header->name, "Content-Type") == 0) {
				strncpy(accumulator->content_type_hdr, header->value,
					sizeof(accumulator->content_type_hdr) - 1);
				accumulator->content_type_hdr[sizeof(accumulator->content_type_hdr) - 1] = '\0';
			}
		}
	}

	/* Null-terminate data on final status */
	if (status == HTTP_SERVER_DATA_FINAL) {
		if (accumulator->data_len < CONFIG_MCP_MAX_MESSAGE_SIZE) {
			accumulator->data[accumulator->data_len] = '\0';
		} else {
			LOG_WRN("Cannot null-terminate accumulator: buffer full");
		}
	}

	return 0;
}

/*******************************************************************************
 * Client context helpers
 ******************************************************************************/
static struct mcp_http_client_ctx *allocate_client(uint32_t session_id)
{
	struct mcp_http_client_ctx *client = NULL;
	int ret;

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return client;
	}

	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (!http_transport_state.clients[i].in_use) {
			client = &http_transport_state.clients[i];
			client->session_id = session_id;
			client->in_use = true;
			client->next_event_id = 0;
			snprintf(client->session_id_str, sizeof(client->session_id_str), "%" PRIx32,
				 client->session_id);

			/* Initialize response queue */
			k_fifo_init(&client->response_queue);

			break;
		}
	}

	k_mutex_unlock(&http_transport_state.clients_mutex);

	if (!client) {
		LOG_ERR("No available client slots");
	}

	return client;
}

static int release_client(struct mcp_http_client_ctx *client)
{
	int ret;
	struct mcp_http_response_item *item;

	if (!client) {
		return -EINVAL;
	}

	/* Clean up any pending responses in the queue */
	while ((item = k_fifo_get(&client->response_queue, K_NO_WAIT)) != NULL) {
		if (item->data) {
			mcp_free(item->data);
		}
		mcp_free(item);
	}

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return ret;
	}

	client->in_use = false;
	client->next_event_id = 0;
	client->session_id = 0;

	k_mutex_unlock(&http_transport_state.clients_mutex);

	return 0;
}

int mcp_server_http_new_client_handler(struct mcp_transport_binding *binding, uint32_t client_id)
{
	struct mcp_http_client_ctx *client_ctx = allocate_client(client_id);
	if (client_ctx == NULL) {
		return -ENOMEM;
	}

	binding->ops = &mcp_http_transport_ops;
	binding->context = (void *)client_ctx;

	return 0;
}

/*******************************************************************************
 * Helper functions to format response body
 ******************************************************************************/

/**
 * Format a JSON response body
 *
 * @param buffer Output buffer for formatted JSON response
 * @param buffer_size Size of the output buffer
 * @param json_data JSON data string to copy into the buffer
 * @return Length of formatted message on success, negative error code on failure
 */
static int format_response(char *buffer, size_t buffer_size, const char *json_data)
{
	int len;

	if (!buffer || buffer_size == 0 || !json_data) {
		return -EINVAL;
	}

	len = snprintf(buffer, buffer_size, "%s", json_data);

	if (len < 0 || (size_t)len >= buffer_size) {
		LOG_ERR("Failed to format JSON response (length: %d)", len);
		return -ENOSPC;
	}

	return len;
}


/**
 * Format a Server-Sent Events (SSE) response with event ID and data
 *
 * @param buffer Output buffer for formatted SSE message
 * @param buffer_size Size of the output buffer
 * @param event_id Event ID to include in the SSE message
 * @param data Optional data payload (can be NULL for empty data)
 * @return Length of formatted message on success, negative error code on failure
 */
static int format_sse_response(char *buffer, size_t buffer_size, int event_id, const char *data)
{
	int len;

	if (!buffer || (buffer_size == 0)) {
		return -EINVAL;
	}

	if (data && (strlen(data) > 0)) {
		len = snprintf(buffer, buffer_size, "id: %d\ndata: %s\n\n", event_id, data);
	} else {
		len = snprintf(buffer, buffer_size, "id: %d\ndata:\n\n", event_id);
	}

	if ((len < 0) || ((size_t)len >= buffer_size)) {
		LOG_ERR("Failed to format SSE response (event_id: %d)", event_id);
		return -ENOSPC;
	}

	return len;
}

/**
 * Format a Server-Sent Events (SSE) retry response
 * Sets the retry interval for SSE reconnection attempts
 *
 * @param buffer Output buffer for formatted SSE retry message
 * @param buffer_size Size of the output buffer
 * @param retry_ms Retry interval in milliseconds
 * @return Length of formatted message on success, negative error code on failure
 */
static int format_sse_retry_response(char *buffer, size_t buffer_size, uint32_t retry_ms)
{
	int len;

	if (!buffer || (buffer_size == 0)) {
		return -EINVAL;
	}

	len = snprintf(buffer, buffer_size, "retry: %u\n\n", retry_ms);

	if ((len < 0) || ((size_t)len >= buffer_size)) {
		LOG_ERR("Failed to format SSE retry response (retry: %u ms)", retry_ms);
		return -ENOSPC;
	}

	return len;
}

static void cleanup_response_item(struct mcp_http_response_item *item)
{
	if (item) {
		if (item->data) {
			mcp_free(item->data);
		}
		mcp_free(item);
	}
}
/*******************************************************************************
 * POST Handler
 ******************************************************************************/
static int mcp_endpoint_post_handler(struct http_client_ctx *client,
					 const struct http_request_ctx *request_ctx,
					 struct mcp_http_request_accumulator *accumulator,
					 struct http_response_ctx *response_ctx)
{
	int ret;
	enum mcp_method msg_type;
	struct mcp_http_client_ctx *mcp_client_ctx;
	struct mcp_transport_binding *binding;
	struct mcp_http_response_item *response_data = NULL;

	struct mcp_request_data request_data = {
		.json_data = accumulator->data,
		.json_len = accumulator->data_len,
		.client_id_hint = accumulator->session_id_hdr,
		.callback = mcp_server_http_new_client_handler
	};

	ret = mcp_server_handle_request(http_transport_state.server_core, &request_data, &msg_type,
					&binding);
	if (ret) {
		LOG_ERR("Error processing request: %d", ret);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return -EINVAL;
	}

	mcp_client_ctx = (struct mcp_http_client_ctx *)binding->context;

	if (!mcp_client_ctx) {
		LOG_ERR("Client session not found for session ID: %" PRIx32,
			accumulator->session_id_hdr);
		response_ctx->status = HTTP_400_BAD_REQUEST;
		return -ENOENT;
	}

	/* Setup common response headers */
	mcp_client_ctx->response_headers[0].name = "Content-Type";
	mcp_client_ctx->response_headers[0].value = "application/json";
	mcp_client_ctx->response_headers[1].name = "Mcp-Session-Id";
	mcp_client_ctx->response_headers[1].value = (const char *)mcp_client_ctx->session_id_str;

	response_ctx->headers = mcp_client_ctx->response_headers;
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;
	response_ctx->header_count = 2;

	switch (msg_type) {
	case MCP_METHOD_UNKNOWN:
	case MCP_METHOD_PING:
	case MCP_METHOD_TOOLS_LIST:
	case MCP_METHOD_INITIALIZE:
		/**
		 * These methods don't take long to process so we can respond within
		 * the POST handler.
		 */
		response_data = k_fifo_get(&mcp_client_ctx->response_queue, K_FOREVER);

		ret = format_response(mcp_client_ctx->response_body,
					   sizeof(mcp_client_ctx->response_body),
					   response_data->data);
		cleanup_response_item(response_data);

		if (ret < 0) {
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			return ret;
		}

		response_ctx->body = (const char *)mcp_client_ctx->response_body;
		response_ctx->body_len = ret;

		LOG_DBG("Serialized response: %s", response_ctx->body);
		break;
	case MCP_METHOD_NOTIF_INITIALIZED:
		/* Notifications don't require a response body */
		response_ctx->status = HTTP_202_ACCEPTED;
		break;
	case MCP_METHOD_TOOLS_CALL:
		/**
		 * Tools can take a long time to process and generate a response
		 * so we use SSE for this.
		 */
		mcp_client_ctx->response_headers[0].value = "text/event-stream";

		ret = format_sse_response(mcp_client_ctx->response_body,
					  sizeof(mcp_client_ctx->response_body),
					  mcp_client_ctx->next_event_id++,
					  NULL);

		if (ret < 0) {
			LOG_ERR("Failed to format SSE response: %d", ret);
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			return ret;
		}

		response_ctx->body = mcp_client_ctx->response_body;
		response_ctx->body_len = ret;
		break;
	default:
		LOG_WRN("Unhandled method type: %d", msg_type);
		break;
	}

	return 0;
}

/*******************************************************************************
 * GET Handler
 ******************************************************************************/
static int mcp_endpoint_get_handler(struct http_client_ctx *client,
					const struct http_request_ctx *request_ctx,
					struct mcp_http_request_accumulator *accumulator,
					struct http_response_ctx *response_ctx)
{
	int ret;
	struct mcp_http_client_ctx *mcp_client_ctx;
	struct mcp_http_response_item *response_data;

	/* Find client based on session id */
	struct mcp_transport_binding *binding = mcp_server_get_client_binding(
		http_transport_state.server_core, accumulator->session_id_hdr);
	if (binding == NULL) {
		LOG_ERR("Client session not found for session ID: %" PRIx32,
			accumulator->session_id_hdr);
		response_ctx->status = HTTP_400_BAD_REQUEST;
		response_ctx->final_chunk = true;
		return -ENOENT;
	}

	mcp_client_ctx = (struct mcp_http_client_ctx *)binding->context;

	mcp_client_ctx->response_headers[0].name = "Content-Type";
	mcp_client_ctx->response_headers[0].value = "text/event-stream";
	mcp_client_ctx->response_headers[1].name = "Mcp-Session-Id";
	mcp_client_ctx->response_headers[1].value = (const char *)mcp_client_ctx->session_id_str;

	response_ctx->headers = mcp_client_ctx->response_headers;
	response_ctx->header_count = 2;
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	/* Check if queue has response */
	response_data = k_fifo_peek_head(&mcp_client_ctx->response_queue);

	if (response_data == NULL || !accumulator->has_event_id ||
		(response_data->event_id < accumulator->event_id_hdr)) {
		/* Send retry interval when there are no events to stream */
		ret = format_sse_retry_response(mcp_client_ctx->response_body,
						sizeof(mcp_client_ctx->response_body),
						CONFIG_MCP_HTTP_SSE_RETRY_MS);
		if (ret < 0) {
			LOG_ERR("Failed to format SSE retry response: %d", ret);
			response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			return ret;
		}

		response_ctx->body = mcp_client_ctx->response_body;
		response_ctx->body_len = ret;

		LOG_DBG("Sending retry event: %s", response_ctx->body);
		return 0;
	}

	/* Format SSE response with data */
	response_data = k_fifo_get(&mcp_client_ctx->response_queue, K_NO_WAIT);
	ret = format_sse_response(mcp_client_ctx->response_body,
				  sizeof(mcp_client_ctx->response_body),
				  response_data->event_id,
				  response_data->data);

	if (ret < 0) {
		LOG_ERR("Failed to format SSE response: %d", ret);
		cleanup_response_item(response_data);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		return ret;
	}

	response_ctx->body = mcp_client_ctx->response_body;
	response_ctx->body_len = ret;

	cleanup_response_item(response_data);

	LOG_DBG("Sending SSE response: %s", response_ctx->body);
	return 0;
}

/*******************************************************************************
 * HTTP resource handler
 ******************************************************************************/
static int mcp_server_http_resource_handler(struct http_client_ctx *client,
						enum http_data_status status,
						const struct http_request_ctx *request_ctx,
						struct http_response_ctx *response_ctx, void *user_data)
{
	int stat = 0;

	/* TODO: Validate origin */

	struct mcp_http_request_accumulator *accumulator = get_accumulator(client->fd);
	if (accumulator == NULL) {
		LOG_ERR("Failed to allocate mcp accumualator fd=%d", client->fd);
		return -ENOMEM;
	}

	if (status == HTTP_SERVER_DATA_ABORTED) {
		/* Request aborted, clean up accumulator */
		LOG_WRN("HTTP request aborted for client fd=%d", client->fd);
		release_accumulator(accumulator);
		return stat;
	}

	accumulate_request(client, accumulator, request_ctx, status);

	if (status == HTTP_SERVER_DATA_FINAL) {
		LOG_DBG("HTTP %s request: %s", client->method == HTTP_POST ? "POST" : "GET", accumulator->data);
		/* Process complete request */
		if (client->method == HTTP_POST) {
			stat = mcp_endpoint_post_handler(client, request_ctx, accumulator,
							 response_ctx);
		} else if (client->method == HTTP_GET) {
			stat = mcp_endpoint_get_handler(client, request_ctx, accumulator,
							response_ctx);
		} else {
			LOG_WRN("Unsupported HTTP method");
			return -ENOTSUP;
		}

		release_accumulator(accumulator);
	}

	return stat;
}

/*******************************************************************************
 * Interface Implementation
 ******************************************************************************/
int mcp_server_http_init(mcp_server_ctx_t server_ctx)
{
	int ret;

	LOG_INF("Initializing HTTP/SSE transport");

	if (http_transport_state.initialized) {
		LOG_WRN("HTTP transport already initialized");
		return 0;
	}

	/* Initialize client slots */
	ret = k_mutex_init(&http_transport_state.clients_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to initialize clients mutex: %d", ret);
		return ret;
	}

	for (int i = 0; i < ARRAY_SIZE(http_transport_state.clients); i++) {
		http_transport_state.clients[i].in_use = false;
	}

	/* Initialize accumulator slots */
	ret = k_mutex_init(&http_transport_state.accumulators_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to initialize accumulators mutex: %d", ret);
		return ret;
	}

	for (int i = 0; i < ARRAY_SIZE(http_transport_state.accumulators); i++) {
		http_transport_state.accumulators[i].fd = -1;
		http_transport_state.accumulators[i].data_len = 0;
	}

	http_transport_state.server_core = server_ctx;
	http_transport_state.initialized = true;

	return 0;
}

int mcp_server_http_start(mcp_server_ctx_t server_ctx)
{
	if ((server_ctx == NULL) || (server_ctx != http_transport_state.server_core) ||
		!http_transport_state.initialized) {
		LOG_ERR("HTTP server context invalid or transport not initialized");
		return -EINVAL;
	}

	/* Start HTTP server */
	int ret = http_server_start();
	if (ret != 0) {
		LOG_ERR("Failed to start HTTP server: %d", ret);
		return ret;
	}

	LOG_INF("HTTP transport running on port %d, endpoint: %s", CONFIG_MCP_HTTP_PORT,
		CONFIG_MCP_HTTP_ENDPOINT);

	return 0;
}

static int mcp_server_http_send(struct mcp_transport_binding *binding, uint32_t client_id,
				const void *data, size_t length)
{
	struct mcp_http_response_item *item;
	struct mcp_http_client_ctx *client;
	int ret;

	if (!http_transport_state.initialized) {
		LOG_WRN("HTTP transport not initialized");
		return -ENODEV;
	}

	if ((binding == NULL) || (data == NULL) || length == 0) {
		LOG_ERR("Invalid send parameters");
		return -EINVAL;
	}

	ret = k_mutex_lock(&http_transport_state.clients_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock clients mutex: %d", ret);
		return ret;
	}

	client = (struct mcp_http_client_ctx *)binding->context;
	if (!client || !client->in_use) {
		k_mutex_unlock(&http_transport_state.clients_mutex);
		LOG_ERR("Client %u not found or not in use", client_id);
		return -ENOENT;
	}

	k_mutex_unlock(&http_transport_state.clients_mutex);

	item = (struct mcp_http_response_item *)mcp_alloc(sizeof(struct mcp_http_response_item));
	if (!item) {
		LOG_ERR("Failed to allocate response item");
		return -ENOMEM;
	}

	/* Take ownership of the data pointer */
	item->data = (char *)data;
	item->length = length;
	item->event_id = client->next_event_id++;

	/* POST/GET handlers are responsible for freeing response items and data from core */
	k_fifo_put(&client->response_queue, item);

	return 0;
}

static int mcp_server_http_disconnect(struct mcp_transport_binding *binding, uint32_t client_id)
{
	if (!http_transport_state.initialized) {
		LOG_WRN("HTTP transport not initialized");
		return -ENODEV;
	}

	if ((binding == NULL)) {
		LOG_ERR("Invalid send parameters");
		return -EINVAL;
	}

	struct mcp_http_client_ctx *client = (struct mcp_http_client_ctx *)binding->context;

	release_client(client);
	return 0;
}
