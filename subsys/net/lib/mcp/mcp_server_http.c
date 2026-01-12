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

#define SESSION_ID_STR_LEN   ((sizeof(uint32_t) * 2) + 1)
#define CONTENT_TYPE_HDR_LEN (sizeof("text/event-stream") + 1) // worst case for content-type header
#define ORIGIN_HDR_LEN       128
#define MAX_RESPONSE_HEADERS 4 /* Content-Type, Last-Event-Id, Mcp-Session-Id, extra buffer */

/* Request accumulation buffer for each client */
struct mcp_http_request_accumulator {
	char data[CONFIG_MCP_TRANSPORT_BUFFER_SIZE];
	size_t data_len;
	uint32_t session_id_hdr;
	uint32_t last_event_id_hdr;
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
	char response_body[CONFIG_MCP_TRANSPORT_BUFFER_SIZE]; /* TODO: set appropriate size */
	struct k_fifo response_queue;                         /* Response queue for SSE */
	bool in_use;
	bool busy; /* Since we only support one request per client at a time,
				  this is cleared once the server responds to a client request */
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

static struct http_transport_state http_transport_state;

/* Forward declarations */
static int mcp_server_http_resource_handler(struct http_client_ctx *client,
					    enum http_data_status status,
					    const struct http_request_ctx *request_ctx,
					    struct http_response_ctx *response_ctx,
					    void *user_data);
static int mcp_server_http_send(struct mcp_transport_binding *ep, uint32_t client_id,
				const void *data, size_t length);
static int mcp_server_http_disconnect(struct mcp_transport_binding *ep, uint32_t client_id);

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

const struct mcp_transport_ops mcp_http_transport_ops = {
	.send = mcp_server_http_send,
	.disconnect = mcp_server_http_disconnect,
};

/* HTTP resource definition */
HTTP_RESOURCE_DEFINE(mcp_endpoint_resource, mcp_http_service, CONFIG_MCP_HTTP_ENDPOINT,
		     &mcp_resource_detail);

/* HTTP service definition */
HTTP_SERVICE_DEFINE(mcp_http_service, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &mcp_http_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

/* HTTP headers capture */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(origin_hdr, "Origin");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(content_type_hdr, "Content-Type");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(mcp_session_id_hdr, "Mcp-Session-Id");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(last_event_id_hdr, "Last-Event-Id");

/**
 * @brief Get or allocate an accumulator for the given file descriptor
 * This function searches for an existing accumulator associated with the given fd.
 * If found, it returns a pointer to it. If not found, it allocates the first
 */
static struct mcp_http_request_accumulator *get_accumulator(int fd)
{
	int ret;
	struct mcp_http_request_accumulator *first_available = NULL;

	ret = k_mutex_lock(&http_transport_state.accumulators_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock accumulator mutex: %d", ret);
		return NULL;
	}

	for (int i = 0; i < ARRAY_SIZE(http_transport_state.accumulators); i++) {

		if (http_transport_state.accumulators[i].fd == fd) {
			k_mutex_unlock(&http_transport_state.accumulators_mutex);
			return &http_transport_state.accumulators[i];
		}

		if ((first_available == NULL) && (http_transport_state.accumulators[i].fd == -1)) {
			first_available = &http_transport_state.accumulators[i];
		}
	}

	if (first_available != NULL) {
		first_available->fd = fd;
		first_available->data_len = 0;
	}

	k_mutex_unlock(&http_transport_state.accumulators_mutex);
	return first_available;
}

static int release_accumulator(struct mcp_http_request_accumulator *accumulator)
{
	int ret;

	if (!accumulator) {
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

	if (request_ctx->data_len > 0) {
		if ((accumulator->data_len + request_ctx->data_len) >
		    CONFIG_MCP_TRANSPORT_BUFFER_SIZE) {
			LOG_WRN("Accumulator full. Dropping current chunk");
			return -ENOMEM;
		}

		memcpy(accumulator->data + accumulator->data_len, request_ctx->data,
		       request_ctx->data_len);
		accumulator->data_len += request_ctx->data_len;
	}

	if ((request_ctx->headers_status == HTTP_HEADER_STATUS_OK) &&
	    (request_ctx->header_count > 0)) {
		for (uint32_t i = 0; i < request_ctx->header_count; i++) {
			const struct http_header *header = &request_ctx->headers[i];
			if (header->name && header->value) {
				LOG_DBG("Header: %s = %s", header->name, header->value);
				if (strcmp(header->name, "Mcp-Session-Id") == 0) {
					/* Convert hex string to uint32_t */
					char *endptr;
					accumulator->session_id_hdr =
						strtoul(header->value, &endptr, 16);
					if (*endptr != '\0') {
						LOG_ERR("Invalid session ID format: %s",
							header->value);
						return -EINVAL;
					}
					LOG_DBG("Stored session id header: %" PRIx32,
						accumulator->session_id_hdr);
				}

				if (strcmp(header->name, "Last-Event-Id") == 0) {
					/* Convert hex string to uint32_t */
					char *endptr;
					accumulator->session_id_hdr =
						strtoul(header->value, &endptr, 10);
					if (*endptr != '\0') {
						LOG_ERR("Invalid session ID format: %s",
							header->value);
						return -EINVAL;
					}
					LOG_DBG("Stored session id header: %" PRIx32,
						accumulator->session_id_hdr);
				}

				if (strcmp(header->name, "Origin") == 0) {
					if (strlen(header->value) <
					    sizeof(accumulator->origin_hdr)) {
						memcpy(accumulator->origin_hdr, header->value,
						       strlen(header->value));
						accumulator->origin_hdr[strlen(header->value)] =
							'\0';
						LOG_DBG("Stored origin header: %s",
							accumulator->origin_hdr);
					} else {
						LOG_WRN("Session ID too long for buffer");
					}
				}

				if (strcmp(header->name, "Content-Type") == 0) {
					if (strlen(header->value) <
					    sizeof(accumulator->content_type_hdr)) {
						memcpy(accumulator->content_type_hdr, header->value,
						       strlen(header->value));
						accumulator
							->content_type_hdr[strlen(header->value)] =
							'\0';
						LOG_DBG("Stored content type header: %s",
							accumulator->content_type_hdr);
					} else {
						LOG_WRN("Session ID too long for buffer");
					}
				}
			}
		}
	}

	if (status == HTTP_SERVER_DATA_FINAL) {
		if (accumulator->data_len < CONFIG_MCP_TRANSPORT_BUFFER_SIZE) {
			accumulator->data[accumulator->data_len] = '\0';
		} else {
			LOG_WRN("Cannot null-terminate accumulator: buffer full");
		}
	}

	return 0;
}

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
			client->busy = false;
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

/**
 * @brief Release client context
 */
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
	client->busy = false;

	k_mutex_unlock(&http_transport_state.clients_mutex);

	return 0;
}

void mcp_server_http_new_client_handler(struct mcp_transport_binding *ep, uint32_t client_id)
{
	struct mcp_http_client_ctx *client_ctx = allocate_client(client_id);
	ep->ops = &mcp_http_transport_ops;
	ep->context = (void *)client_ctx;
}

/**
 * @brief HTTP POST handler for MCP endpoint
 */
static int mcp_endpoint_post_handler(struct http_client_ctx *client,
				     const struct http_request_ctx *request_ctx,
				     struct mcp_http_request_accumulator *accumulator,
				     struct http_response_ctx *response_ctx)
{
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

	msg_type = mcp_server_handle_request(
		http_transport_state.server_core, &request_data, &binding);

	if (msg_type == MCP_METHOD_UNKNOWN || binding == NULL) {
		LOG_ERR("Invalid request: %d", msg_type);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return -EINVAL;
	}

	mcp_client_ctx = (struct mcp_http_client_ctx *)binding->context;

	/* If initialize, create a new client. respond with appropriate headers. Don't send text
	 * event, block here and wait for response from server core */
	if (msg_type == MCP_METHOD_INITIALIZE) {
		LOG_DBG("CLient session ID is %x", mcp_client_ctx->session_id);

		mcp_client_ctx->response_headers[0].name = "Content-Type";
		mcp_client_ctx->response_headers[0].value = "application/json";
		mcp_client_ctx->response_headers[1].name = "Mcp-Session-Id";
		mcp_client_ctx->response_headers[1].value =
			(const char *)mcp_client_ctx->session_id_str;

		response_ctx->headers = mcp_client_ctx->response_headers;
		response_ctx->header_count = 2;

		/* Wait for response from server core before sending response to client */
		response_data = k_fifo_get(&mcp_client_ctx->response_queue, K_FOREVER);
		memcpy(mcp_client_ctx->response_body, response_data->data, response_data->length);

		response_ctx->body = (const char *)mcp_client_ctx->response_body;
		response_ctx->body_len = response_data->length;
		response_ctx->status = HTTP_200_OK;
		response_ctx->final_chunk = true;

		mcp_free(response_data->data);
		mcp_free(response_data);
	} else {
		/* Verify client session exists for non-initialize requests. If not found,
		 * the session has expired and the client must re-initialize. */

		if (!mcp_client_ctx) {
			LOG_ERR("Client session not found for session ID: %" PRIx32,
				accumulator->session_id_hdr);
			response_ctx->status = HTTP_400_BAD_REQUEST;
			return -ENOENT;
		}

		if (mcp_client_ctx->busy) {
			LOG_WRN("Client is busy processing previous request");
			response_ctx->status = HTTP_429_TOO_MANY_REQUESTS;
			return -EBUSY;
		}

		response_ctx->status = HTTP_200_OK;

		if (msg_type == MCP_METHOD_TOOLS_LIST) {
			/* Wait for the response since there is no tool call needed */
			response_data = k_fifo_get(&mcp_client_ctx->response_queue, K_FOREVER);
			mcp_client_ctx->response_headers[0].name = "Content-Type";
			mcp_client_ctx->response_headers[0].value = "application/json";
			memcpy(mcp_client_ctx->response_body, response_data->data,
			       response_data->length);
			response_ctx->body = (const char *)mcp_client_ctx->response_body;
			response_ctx->body_len = response_data->length;
		}

		if (msg_type == MCP_METHOD_TOOLS_CALL) {
			mcp_client_ctx->response_headers[0].name = "Content-Type";
			mcp_client_ctx->response_headers[0].value = "text/event-stream";
			int body_len = snprintf(mcp_client_ctx->response_body,
						sizeof(mcp_client_ctx->response_body),
						"\"id\": \"%d\" \"data\": {}",
						mcp_client_ctx->next_event_id++);
			response_ctx->body = mcp_client_ctx->response_body;
			response_ctx->body_len = body_len;
			mcp_client_ctx->busy = true;
		}

		response_ctx->headers = mcp_client_ctx->response_headers;
		response_ctx->header_count = 2;

		mcp_client_ctx->response_headers[1].name = "Mcp-Session-Id";
		mcp_client_ctx->response_headers[1].value =
			(const char *)mcp_client_ctx->session_id_str;
		response_ctx->final_chunk = true;
	}

	return 0;
}

/**
 * @brief HTTP GET handler for MCP endpoint
 */
static int mcp_endpoint_get_handler(struct http_client_ctx *client,
				    const struct http_request_ctx *request_ctx,
				    struct mcp_http_request_accumulator *accumulator,
				    struct http_response_ctx *response_ctx)
{

	/* Find client based on session id */
	struct mcp_transport_binding *binding = mcp_server_get_client_binding(
		http_transport_state.server_core, accumulator->session_id_hdr);
	if (!binding) {
		LOG_ERR("Client session not found for session ID: %" PRIx32,
			accumulator->session_id_hdr);
		response_ctx->status = HTTP_400_BAD_REQUEST;
		response_ctx->final_chunk = true;
		response_ctx->body_len = 0;
		return -ENOENT;
	}

	struct mcp_http_client_ctx *mcp_client_ctx = (struct mcp_http_client_ctx *)binding->context;

	/* Check if queue has response */
	struct mcp_http_response_item *response_data =
		k_fifo_peek_head(&mcp_client_ctx->response_queue);

	if (response_data == NULL) {
		LOG_DBG("No response data available in queue");
		response_ctx->status = HTTP_204_NO_CONTENT;
		response_ctx->body_len = 0;
		response_ctx->final_chunk = true;
		return 0;
	}

	if (response_data->event_id < accumulator->last_event_id_hdr) {
		LOG_DBG("Event ID %d matches or exceeds last event ID %d", response_data->event_id,
			accumulator->last_event_id_hdr);
		response_ctx->status = HTTP_204_NO_CONTENT;
		response_ctx->body_len = 0;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_data = k_fifo_get(&mcp_client_ctx->response_queue, K_NO_WAIT);

	/* Send response data. TODO: What happens if resp_data->data is larger than
		* response_body buffer? Do we chunk it? */
	LOG_DBG("Sending response with event ID %d", response_data->event_id);
	int body_len = snprintf(mcp_client_ctx->response_body,
				sizeof(mcp_client_ctx->response_body), "\"id\": \"%d\" %s",
				response_data->event_id, response_data->data);
	response_ctx->body = mcp_client_ctx->response_body;
	response_ctx->body_len = body_len;
	response_ctx->status = HTTP_200_OK;
	response_ctx->final_chunk = true;

	mcp_client_ctx->response_headers[0].name = "Content-Type";
	mcp_client_ctx->response_headers[0].value = "text/event-stream";
	mcp_client_ctx->response_headers[1].name = "Mcp-Session-Id";
	mcp_client_ctx->response_headers[1].value =
		(const char *)mcp_client_ctx->session_id_str;

	response_ctx->headers = mcp_client_ctx->response_headers;
	response_ctx->header_count = 2;

	mcp_client_ctx->busy = false;
	mcp_free(response_data->data);
	mcp_free(response_data);

	return 0;
}

/**
 * @brief HTTP resource handler wrapper
 */
static int mcp_server_http_resource_handler(struct http_client_ctx *client,
					    enum http_data_status status,
					    const struct http_request_ctx *request_ctx,
					    struct http_response_ctx *response_ctx, void *user_data)
{
	int stat = 0;

	/* TODO: Validate origin */

	struct mcp_http_request_accumulator *accumulator = get_accumulator(client->fd);
	if (!accumulator) {
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
/**
 * @brief Initialize HTTP transport
 */
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
	if (!server_ctx || (server_ctx != http_transport_state.server_core) ||
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

/**
 * @brief Send data to a client
 */
static int mcp_server_http_send(struct mcp_transport_binding *ep, uint32_t client_id,
				const void *data, size_t length)
{
	struct mcp_http_response_item *item;

	if (!http_transport_state.initialized) {
		LOG_WRN("HTTP transport not initialized");
		return -ENODEV;
	}

	if (!ep || !data || length == 0) {
		LOG_ERR("Invalid send parameters");
		return -EINVAL;
	}

	/* Find the client */
	struct mcp_http_client_ctx *client = (struct mcp_http_client_ctx *)ep->context;
	if (!client) {
		LOG_ERR("Client %u not found", client_id);
		return -ENOENT;
	}

	/* Allocate response item */
	item = (struct mcp_http_response_item *)mcp_alloc(sizeof(struct mcp_http_response_item));
	if (!item) {
		LOG_ERR("Failed to allocate response item");
		return -ENOMEM;
	}

	/* Take ownership of the data pointer */
	item->data = (char *)data;
	item->length = length;
	item->event_id = client->next_event_id++;

	/* Add to client's response queue */
	k_fifo_put(&client->response_queue, item);

	LOG_DBG("Queued %zu bytes for client %u (event_id=%u)", length, client_id, item->event_id);

	return 0;
}

/**
 * @brief Disconnect a client
 * @param client_id Client identifier
 * @return 0 on success, negative errno on failure
 */
static int mcp_server_http_disconnect(struct mcp_transport_binding *ep, uint32_t client_id)
{
	if (!http_transport_state.initialized) {
		LOG_WRN("HTTP transport not initialized");
		return -ENODEV;
	}

	if (!ep) {
		LOG_ERR("Invalid send parameters");
		return -EINVAL;
	}

	struct mcp_http_client_ctx *client = (struct mcp_http_client_ctx *)ep->context;

	release_client(client);
	return 0;
}
