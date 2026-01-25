/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <zephyr/random/random.h>
#include <errno.h>
#include "mcp_common.h"
#include "mcp_server_internal.h"
#include "mcp_json.h"

LOG_MODULE_REGISTER(mcp_server, CONFIG_MCP_LOG_LEVEL);

#define MCP_WORKER_PRIORITY  7
#define MCP_MAX_REQUESTS     (CONFIG_HTTP_SERVER_MAX_CLIENTS * CONFIG_HTTP_SERVER_MAX_STREAMS)
#define MCP_SERVER_VERSION   "1.0.0"
#define MCP_PROTOCOL_VERSION "2025-11-25"

enum mcp_lifecycle_state {
	MCP_LIFECYCLE_DEINITIALIZED = 0,
	MCP_LIFECYCLE_NEW,
	MCP_LIFECYCLE_INITIALIZING,
	MCP_LIFECYCLE_INITIALIZED,
	MCP_LIFECYCLE_DEINITIALIZING
};

enum mcp_execution_state {
	MCP_EXEC_ACTIVE,
	MCP_EXEC_CANCELED,
	MCP_EXEC_FINISHED,
	MCP_EXEC_ZOMBIE
};

struct mcp_queue_msg {
	uint32_t client_id;
	void *data;
};

struct mcp_tool_registry {
	struct mcp_tool_record tools[CONFIG_MCP_MAX_TOOLS];
	struct k_mutex registry_mutex;
	uint8_t tool_count;
};

struct mcp_execution_context {
	uint32_t execution_token;
	uint32_t request_id;
	uint32_t client_id;
	k_tid_t worker_id;
	int64_t start_timestamp;
	int64_t cancel_timestamp;
	int64_t last_message_timestamp;
	bool worker_released;
	enum mcp_execution_state execution_state;
};

struct mcp_execution_registry {
	struct mcp_execution_context executions[MCP_MAX_REQUESTS];
	struct k_mutex registry_mutex;
};

struct mcp_client_context {
	uint32_t client_id;
	enum mcp_lifecycle_state lifecycle_state;
	uint32_t active_requests[CONFIG_HTTP_SERVER_MAX_STREAMS];
	uint8_t active_request_count;
	struct mcp_transport_binding transport_binding;
};

struct mcp_client_registry {
	struct mcp_client_context clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex registry_mutex;
	uint8_t client_count;
};

/**
 * @brief MCP server context structure
 * @details Contains all state and resources needed to manage an MCP server instance,
 * including client registry, transport operations, request processing workers,
 * and message queues for handling MCP protocol requests and responses.
 */
struct mcp_server_ctx {
	uint8_t idx;
	bool in_use;
	struct mcp_client_registry client_registry;
	struct k_thread request_workers[CONFIG_MCP_REQUEST_WORKERS];
	struct k_msgq request_queue;
	char request_queue_buffers[CONFIG_MCP_REQUEST_QUEUE_SIZE * sizeof(struct mcp_queue_msg)];
	struct mcp_tool_registry tool_registry;
	struct mcp_execution_registry execution_registry;
#ifdef CONFIG_MCP_HEALTH_MONITOR
	struct k_thread health_monitor_thread;
#endif
};

static struct mcp_server_ctx mcp_servers[CONFIG_MCP_SERVER_COUNT];

K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks,
			    CONFIG_MCP_REQUEST_WORKERS *CONFIG_MCP_SERVER_COUNT, 2048);
#ifdef CONFIG_MCP_HEALTH_MONITOR
K_THREAD_STACK_ARRAY_DEFINE(mcp_health_monitor_stack, CONFIG_MCP_SERVER_COUNT, 2048);
#endif

/*******************************************************************************
 * Server Context Helper Functions
 ******************************************************************************/
static struct mcp_server_ctx *allocate_mcp_server_context(void)
{
	for (uint32_t i = 0; i < ARRAY_SIZE(mcp_servers); i++) {
		if (!mcp_servers[i].in_use) {
			memset(&mcp_servers[i], 0, sizeof(struct mcp_server_ctx));
			mcp_servers[i].idx = i;
			mcp_servers[i].in_use = true;
			return &mcp_servers[i];
		}
	}

	return NULL;
}

/*******************************************************************************
 * Client Context Helper Functions
 ******************************************************************************/
static int generate_client_id(struct mcp_server_ctx *server, uint32_t *client_id)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	*client_id = INVALID_CLIENT_ID;

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock registry mutex: %d", ret);
		return -EBUSY;
	}

	do {
		*client_id = sys_rand32_get();
		for (uint32_t i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
			if ((client_registry->clients[i].lifecycle_state !=
			     MCP_LIFECYCLE_DEINITIALIZED) &&
			    (client_registry->clients[i].client_id == *client_id)) {
				*client_id = 0;
			}
		}
	} while (*client_id == INVALID_CLIENT_ID);

	k_mutex_unlock(&client_registry->registry_mutex);

	return 0;
}

static struct mcp_client_context *get_client(struct mcp_server_ctx *server, uint32_t client_id)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	if (client_id == INVALID_CLIENT_ID) {
		return NULL;
	}

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].client_id == client_id) {
			return &client_registry->clients[i];
		}
	}

	return NULL;
}

static struct mcp_client_context *add_client(struct mcp_server_ctx *server)
{
	struct mcp_client_registry *client_registry = &server->client_registry;
	uint32_t client_id;

	/* Assign a new client id */
	if (generate_client_id(server, &client_id)) {
		LOG_ERR("Unable to geenrate session id");
		return NULL;
	}

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].client_id == 0) {
			client_registry->clients[i].client_id = client_id;
			client_registry->clients[i].lifecycle_state = MCP_LIFECYCLE_NEW;
			client_registry->clients[i].active_request_count = 0;
			client_registry->client_count++;
			return &client_registry->clients[i];
		}
	}

	return NULL;
}

/* Must be called with registry_mutex held */
static void remove_client(struct mcp_server_ctx *server, struct mcp_client_context *client)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	client->transport_binding.ops->disconnect(&client->transport_binding, client->client_id);
	client->client_id = 0;
	client->active_request_count = 0;

	memset(client, 0, sizeof(struct mcp_client_context));

	client->lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;
	client_registry->client_count--;
}

/*******************************************************************************
 * Execution Context Helper Functions
 ******************************************************************************/
static uint32_t generate_execution_token(uint32_t request_id)
{
	/* Mocking the generation for the 1st phase of development. Security phase
	 * will replace this with UUID generation to make token guessing harder.
	 */
	return request_id;
}

static struct mcp_execution_context *get_execution_context(struct mcp_server_ctx *server,
							   const uint32_t execution_token)
{
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		if (execution_registry->executions[i].execution_token == execution_token) {
			return &execution_registry->executions[i];
		}
	}

	return NULL;
}

static struct mcp_execution_context *add_execution_context(struct mcp_server_ctx *server,
							   uint32_t request_id, uint32_t client_id)
{
	int ret;
	struct mcp_execution_context *context = NULL;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return NULL;
	}

	uint32_t execution_token = generate_execution_token(request_id);

	bool found_slot = false;

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		context = &execution_registry->executions[i];
		if (context->execution_token == 0) {
			context->execution_token = execution_token;
			context->request_id = request_id;
			context->client_id = client_id;
			context->worker_id = k_current_get();
			context->start_timestamp = k_uptime_get();
			context->cancel_timestamp = 0;
			context->last_message_timestamp = k_uptime_get();
			context->worker_released = false;
			context->execution_state = MCP_EXEC_ACTIVE;
			found_slot = true;
			break;
		}
	}

	k_mutex_unlock(&execution_registry->registry_mutex);

	if (!found_slot) {
		LOG_ERR("Execution registry full");
		return NULL;
	}

	return context;
}

static int remove_execution_context(struct mcp_server_ctx *server,
				    struct mcp_execution_context *execution_context)
{
	int ret;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	memset(execution_context, 0, sizeof(struct mcp_execution_context));

	k_mutex_unlock(&execution_registry->registry_mutex);

	return 0;
}

static int set_worker_released_execution_context(struct mcp_server_ctx *server,
						 struct mcp_execution_context *exec_ctx)
{
	int ret;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);

		return ret;
	}
	LOG_DBG("Releasing worker for execution token: %d", exec_ctx->execution_token);
	exec_ctx->worker_released = true;
	LOG_DBG("Released worker for execution token: %d", exec_ctx->execution_token);
	k_mutex_unlock(&execution_registry->registry_mutex);
	return 0;
}

/*******************************************************************************
 * Request Context Helper Functions
 ******************************************************************************/
static uint32_t *get_request(struct mcp_client_context *client, uint32_t request_id)
{
	for (int i = 0; i < ARRAY_SIZE(client->active_requests); i++) {
		if (client->active_requests[i] == request_id) {
			return &client->active_requests[i];
		}
	}

	return NULL;
}

static uint32_t *add_request(struct mcp_client_context *client, uint32_t request_id)
{
	for (int i = 0; i < ARRAY_SIZE(client->active_requests); i++) {
		if (client->active_requests[i] == 0) {
			client->active_requests[i] = request_id;
			client->active_request_count++;
			return &client->active_requests[i];
		}
	}

	return NULL;
}

/*******************************************************************************
 * Tools Context Helper Functions
 ******************************************************************************/
static struct mcp_tool_record *get_tool(struct mcp_server_ctx *server, const char *tool_name)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	for (int i = 0; i < ARRAY_SIZE(tool_registry->tools); i++) {
		if (tool_registry->tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry->tools[i].metadata.name, tool_name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			return &tool_registry->tools[i];
		}
	}

	return NULL;
}

static struct mcp_tool_record *add_tool(struct mcp_server_ctx *server,
					const struct mcp_tool_record *tool_info)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	for (int i = 0; i < ARRAY_SIZE(tool_registry->tools); i++) {
		if (tool_registry->tools[i].metadata.name[0] == '\0') {
			memcpy(&tool_registry->tools[i], tool_info, sizeof(struct mcp_tool_record));
			tool_registry->tool_count++;
			return &tool_registry->tools[i];
		}
	}

	return NULL;
}

static int copy_tool_metadata_to_response(struct mcp_server_ctx *server,
					  struct mcp_result_tools_list *response_data)
{
	struct mcp_tool_metadata *tool_info;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;
	char *buf = response_data->tools_json;
	size_t buf_size = sizeof(response_data->tools_json);
	size_t offset = 0;
	int ret;

	for (int i = 0; i < tool_registry->tool_count; i++) {
		tool_info = &tool_registry->tools[i].metadata;

		ret = snprintf(buf + offset, buf_size - offset,
			       "%s{"
			       "\"name\":\"%s\","
#ifdef CONFIG_MCP_TOOL_TITLE
			       "\"title\":\"%s\","
#endif
#ifdef CONFIG_MCP_TOOL_DESC
			       "\"description\":\"%s\","
#endif
			       "\"inputSchema\":%s"
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			       ",\"outputSchema\":%s"
#endif
			       "}",
			       (i > 0) ? "," : "", /* Add comma separator except for first item */
			       tool_info->name,
#ifdef CONFIG_MCP_TOOL_TITLE
			       tool_info->title,
#endif
#ifdef CONFIG_MCP_TOOL_DESC
			       tool_info->description,
#endif
			       tool_info->input_schema
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			       ,
			       tool_info->output_schema
#endif
		);

		if (ret < 0 || ret >= buf_size - offset) {
			LOG_ERR("Buffer overflow adding tool %d", i);
			return -ENOMEM;
		}

		offset += ret;
	}

	return 0;
}

/*******************************************************************************
 * Request/Response Handling Functions
 ******************************************************************************/
static int send_error_response(struct mcp_server_ctx *server, uint32_t request_id,
			       uint32_t client_id, int32_t error_code, const char *error_message)
{
	struct mcp_error *error_response;
	int ret;

	error_response = (struct mcp_error *)mcp_alloc(sizeof(struct mcp_error));
	if (error_response == NULL) {
		LOG_ERR("Failed to allocate error response");
		return -ENOMEM;
	}

	strncpy(error_response->message, error_message, sizeof(error_response->message) - 1);

	error_response->code = error_code;
	error_response->message[sizeof(error_response->message) - 1] = '\0';
	error_response->has_data = false; /* no data for now */

	/* Allocate buffer for serialization */
	char *json_buffer = (char *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(error_response);
		return -ENOMEM;
	}

	ret = mcp_json_serialize_error(json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE, true, request_id,
				       error_response);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(error_response);
		mcp_free(json_buffer);
		return ret;
	}

	struct mcp_client_context *client = get_client(server, client_id);
	if (client == NULL) {
		LOG_ERR("Client context not found for client_id: %u", client_id);
		mcp_free(error_response);
		mcp_free(json_buffer);
		return -EINVAL;
	}

	ret = client->transport_binding.ops->send(&client->transport_binding, client_id,
						  json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send error response");
		mcp_free(error_response);
		mcp_free(json_buffer);
		return -EIO;
	}

	mcp_free(error_response);
	return 0;
}

static int handle_initialize_request(struct mcp_server_ctx *server, struct mcp_message *request,
				     new_client_cb transport_callback,
				     struct mcp_client_context **client)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	if (transport_callback == NULL) {
		LOG_ERR("Missing transport callback for new client");
		return -EINVAL;
	}

	if (strcmp(request->req.u.initialize.protocol_version, MCP_PROTOCOL_VERSION) != 0) {
		LOG_WRN("Protocol version mismatch: %s",
			request->req.u.initialize.protocol_version);
		return -EINVAL;
	}

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	struct mcp_client_context *new_client = add_client(server);
	if (new_client == NULL) {
		LOG_ERR("Client registry full");
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOMEM;
	}

	/* Notify the transport layer about the new client */
	transport_callback(&new_client->transport_binding, new_client->client_id);

	/* Transport layer must initialize the transport_binding.ops */
	if ((new_client->transport_binding.ops == NULL) ||
	    (new_client->transport_binding.ops->send == NULL) ||
	    (new_client->transport_binding.ops->disconnect == NULL)) {
		LOG_ERR("Transport binding not initialized");
		remove_client(server, new_client);
		return -EINVAL;
	}

	new_client->lifecycle_state = MCP_LIFECYCLE_INITIALIZING;
	k_mutex_unlock(&client_registry->registry_mutex);

	struct mcp_result_initialize *response_data =
		(struct mcp_result_initialize *)mcp_alloc(sizeof(struct mcp_result_initialize));
	if (response_data == NULL) {
		LOG_ERR("Failed to allocate response");
		remove_client(server, new_client);
		return -ENOMEM;
	}

	strncpy(response_data->server_version, CONFIG_MCP_SERVER_INFO_VERSION,
		sizeof(response_data->server_version) - 1);
	response_data->server_version[sizeof(response_data->server_version) - 1] = '\0';
	strncpy(response_data->server_name, CONFIG_MCP_SERVER_INFO_NAME,
		sizeof(response_data->server_name) - 1);
	response_data->server_name[sizeof(response_data->server_name) - 1] = '\0';
	strncpy(response_data->protocol_version, MCP_PROTOCOL_VERSION,
		sizeof(response_data->protocol_version) - 1);
	response_data->protocol_version[sizeof(response_data->protocol_version) - 1] = '\0';
	strncpy(response_data->capabilities_json, "{\"tools\":{\"listChanged\":false}}",
		sizeof(response_data->capabilities_json) - 1);
	response_data->capabilities_json[sizeof(response_data->capabilities_json) - 1] = '\0';
	response_data->has_capabilities = true;

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(response_data);
		remove_client(server, new_client);
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_initialize_result((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
						   request->id, response_data);
	if (ret < 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		remove_client(server, new_client);
		return ret;
	}

	ret = new_client->transport_binding.ops->send(&new_client->transport_binding,
						      new_client->client_id, json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send initialize response %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		remove_client(server, new_client);
		return -EIO;
	}

	mcp_free(response_data);
	*client = new_client;
	return 0;
}

static int handle_tools_list_request(struct mcp_server_ctx *server, uint32_t client_id,
				     struct mcp_message *request)
{
	struct mcp_result_tools_list *response_data;
	int ret;

	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	LOG_DBG("Processing tools list request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	struct mcp_client_context *client = get_client(server, client_id);
	if (client == NULL) {
		LOG_DBG("Client not found: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOENT;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EACCES;
	}

	k_mutex_unlock(&client_registry->registry_mutex);

	if (ret != 0) {
		return ret;
	}

	response_data =
		(struct mcp_result_tools_list *)mcp_alloc(sizeof(struct mcp_result_tools_list));
	if (response_data == NULL) {
		LOG_ERR("Failed to allocate response");
		return -ENOMEM;
	}

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry");
		mcp_free(response_data);
		return ret;
	}

	copy_tool_metadata_to_response(server, response_data);
	k_mutex_unlock(&tool_registry->registry_mutex);

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(response_data);
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_tools_list_result((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
						   request->id, response_data);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		return ret;
	}

	ret = client->transport_binding.ops->send(&client->transport_binding, client_id,
						  json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send tools list response");
		mcp_free(response_data);
		mcp_free(json_buffer);
		return -EIO;
	}

	mcp_free(response_data);
	return 0;
}

static int handle_tools_call_request(struct mcp_server_ctx *server, uint32_t client_id,
				     struct mcp_message *request)
{
	int ret;
	struct mcp_client_context *client;
	mcp_tool_callback_t callback;

	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	LOG_DBG("Processing tools call request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	client = get_client(server, client_id);
	if (client == NULL) {
		LOG_DBG("Client not found: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOENT;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EACCES;
	}

	uint32_t *request_slot = add_request(client, request->id);
	if (request_slot == NULL) {
		LOG_ERR("No available request slot for client");
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOSPC;
	}

	k_mutex_unlock(&client_registry->registry_mutex);

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		goto cleanup_active_request;
	}

	struct mcp_tool_record *tool = get_tool(server, request->req.u.tools_call.name);
	if (tool == NULL) {
		LOG_ERR("Tool '%s' not found", request->req.u.tools_call.name);
		k_mutex_unlock(&tool_registry->registry_mutex);
		ret = -ENOENT;
		goto cleanup_active_request;
	}

	callback = tool->callback;
	k_mutex_unlock(&tool_registry->registry_mutex);

	struct mcp_execution_context *exec_ctx =
		add_execution_context(server, request->id, client_id);
	if (exec_ctx == NULL) {
		LOG_ERR("Failed to create execution context: %d", ret);
		goto cleanup_active_request;
	}

	ret = callback(request->req.u.tools_call.arguments_json, exec_ctx->execution_token);
	if (ret != 0) {
		LOG_ERR("Tool callback failed: %d", ret);
		remove_execution_context(server, exec_ctx);
		goto cleanup_active_request;
	}

	ret = set_worker_released_execution_context(server, exec_ctx);
	if (ret != 0) {
		LOG_ERR("Setting worker released to true failed: %d", ret);
		/* TODO: Cancel tool execution */
		remove_execution_context(server, exec_ctx);
		goto cleanup_active_request;
	}

	return 0;

cleanup_active_request:
	int final_ret = ret;
	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d. Client registry is broken.",
			ret);
		return ret;
	}

	*request_slot = 0;
	client->active_request_count--;

	k_mutex_unlock(&client_registry->registry_mutex);
	return final_ret;
}

static int handle_notification(struct mcp_server_ctx *server, uint32_t client_id,
			       struct mcp_message *notification)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	LOG_DBG("Processing notification");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry");
		return ret;
	}

	struct mcp_client_context *client = get_client(server, client_id);
	if (client == NULL) {
		LOG_ERR("Client not found %x", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOENT;
	}

	switch (notification->method) {
	case MCP_METHOD_NOTIF_INITIALIZED:
		/* State transition: INITIALIZING -> INITIALIZED */
		if (client->lifecycle_state == MCP_LIFECYCLE_INITIALIZING) {
			client->lifecycle_state = MCP_LIFECYCLE_INITIALIZED;
		} else {
			LOG_ERR("Invalid state transition for client %u", client_id);
			k_mutex_unlock(&client_registry->registry_mutex);
			return -EPERM;
		}
		break;
	case MCP_METHOD_NOTIF_CANCELLED:
		/*TODO: Implement */
	default:
		LOG_ERR("Unknown notification method %u", notification->method);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EINVAL;
	}

	k_mutex_unlock(&client_registry->registry_mutex);
	return 0;
}

static int handle_ping_request(struct mcp_server_ctx *server, uint32_t client_id, struct mcp_message *request)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	LOG_DBG("Processing ping request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	struct mcp_client_context *client = get_client(server, client_id);
	if (client == NULL) {
		LOG_DBG("Client not found: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOENT;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %u", client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EACCES;
	}

	k_mutex_unlock(&client_registry->registry_mutex);

	if (ret != 0) {
		return ret;
	}

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_empty_response((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
						   request->id);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(json_buffer);
		return ret;
	}

	ret = client->transport_binding.ops->send(&client->transport_binding, client_id,
						  json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send tools list response");
		mcp_free(json_buffer);
		return -EIO;
	}

	return 0;
}

/*******************************************************************************
 * Worker threads
 ******************************************************************************/
/**
 * @brief Worker threads
 * @param ctx pointer to server context
 * @param wid worker id
 * @param arg3 NULL
 */
static void mcp_request_worker(void *ctx, void *wid, void *arg3)
{
	struct mcp_queue_msg request;
	struct mcp_message *message;
	int32_t error_code;
	char *error_message;

	int worker_id = POINTER_TO_INT(wid);
	int ret;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	LOG_INF("Request worker %d started", worker_id);

	while (1) {
		ret = k_msgq_get(&server->request_queue, &request, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to get request: %d", ret);
			continue;
		}

		if (request.data == NULL) {
			LOG_ERR("NULL data in request");
			continue;
		}

		message = (struct mcp_message *)request.data;

		switch (message->method) {
		case MCP_METHOD_INITIALIZE:
			LOG_DBG("Should never reach here");
			ret = 0;
			// Handled immmediately in mcp_server_handle_request
			break;
		case MCP_METHOD_NOTIF_INITIALIZED:
			ret = handle_notification(server, request.client_id, message);
			break;
		case MCP_METHOD_TOOLS_LIST:
			ret = handle_tools_list_request(server, request.client_id, message);
			break;
		case MCP_METHOD_TOOLS_CALL:
			ret = handle_tools_call_request(server, request.client_id, message);
			break;
		case MCP_METHOD_NOTIF_CANCELLED:
		case MCP_METHOD_PING:
			/* TODO: Implement. Ignore for now */
			break;
		default:
			/* Should never get here. Requests are validated in
			 * mcp_server_handle_request */
			LOG_ERR("Unknown request");
			mcp_free(request.data);
			continue;
		}

		if ((message->kind != MCP_MSG_NOTIFICATION) && (ret != 0)) {
			switch (ret) {
			case -EALREADY:
				error_code = MCP_ERR_INVALID_PARAMS;
				error_message = "Client already initialized";
				break;
			case -ENOENT:
				error_code = MCP_ERR_METHOD_NOT_FOUND;
				error_message = "Resource not found";
				break;
			case -EPERM:
				error_code = MCP_ERR_INVALID_PARAMS;
				error_message = "Permission denied";
				break;
			case -ENOSPC:
				error_code = MCP_ERR_INTERNAL_ERROR;
				error_message = "Resource exhausted";
				break;
			case -ENOMEM:
				error_code = MCP_ERR_INTERNAL_ERROR;
				error_message = "Memory allocation failed";
				break;
			default:
				error_code = MCP_ERR_INTERNAL_ERROR;
				error_message = "Internal server error";
				break;
			}
			send_error_response(server, message->id, request.client_id, error_code,
					    error_message);
		}

		mcp_free(request.data);
	}
}

#ifdef CONFIG_MCP_HEALTH_MONITOR
/**
 * @brief Health monitor worker thread
 * @param ctx Server context pointer
 * @param arg2 NULL
 * @param arg3 NULL
 */
static void mcp_health_monitor_worker(void *ctx, void *arg2, void *arg3)
{
	int ret;
	int64_t current_time;
	int64_t execution_duration;
	int64_t idle_duration;
	int64_t cancel_duration;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	while (1) {
		k_sleep(K_MSEC(CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS));

		current_time = k_uptime_get();

		ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock execution registry: %d", ret);
			continue;
		}

		for (int i = 0; i < MCP_MAX_REQUESTS; i++) {
			struct mcp_execution_context *context = &execution_registry->executions[i];

			if (context->execution_token == 0) {
				continue;
			}

			if (context->execution_state == MCP_EXEC_CANCELED) {
				cancel_duration = current_time - context->cancel_timestamp;

				if (cancel_duration > CONFIG_MCP_TOOL_CANCEL_TIMEOUT_MS) {
					if (!context->worker_released) {
						LOG_ERR("Execution token %u exceeded cancellation "
							"timeout "
							"(%lld ms). Request ID: %u, Client ID: %u, "
							"Worker ID %u "
							"still not released.",
							context->execution_token, cancel_duration,
							context->request_id, context->client_id,
							(uint32_t)context->worker_id);
					} else {
						LOG_ERR("Execution token %u exceeded cancellation "
							"timeout "
							"(%lld ms). Request ID: %u, Client ID: %u",
							context->execution_token, cancel_duration,
							context->request_id, context->client_id);
					}
					/* TODO: Clean up execution record? */
				}
				continue;
			}

			if (context->execution_state == MCP_EXEC_FINISHED) {
				continue;
			}

			execution_duration = current_time - context->start_timestamp;

			if (execution_duration > CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS) {
				if (!context->worker_released) {
					LOG_WRN("Execution token %u exceeded execution timeout "
						"(%lld ms). Request ID: %u, Client ID: %u, Worker "
						"ID %u "
						"still not released.",
						context->execution_token, execution_duration,
						context->request_id, context->client_id,
						(uint32_t)context->worker_id);
				} else {
					LOG_WRN("Execution token %u exceeded execution timeout "
						"(%lld ms). Request ID: %u, Client ID: %u",
						context->execution_token, execution_duration,
						context->request_id, context->client_id);
				}
				/* TODO: Notify client? */
				context->execution_state = MCP_EXEC_CANCELED;
				context->cancel_timestamp = current_time;
				continue;
			}

			if (context->last_message_timestamp > 0) {
				idle_duration = current_time - context->last_message_timestamp;

				if (idle_duration > CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS) {
					if (!context->worker_released) {
						LOG_WRN("Execution token %u exceeded idle timeout "
							"(%lld ms). Request ID: %u, Client ID: %u, "
							"Worker ID %u "
							"still not released.",
							context->execution_token, idle_duration,
							context->request_id, context->client_id,
							(uint32_t)context->worker_id);
					} else {
						LOG_WRN("Execution token %u exceeded idle timeout "
							"(%lld ms). Request ID: %u, Client ID: %u",
							context->execution_token, idle_duration,
							context->request_id, context->client_id);
					}
					/* TODO: Notify client? */
					context->execution_state = MCP_EXEC_CANCELED;
					context->cancel_timestamp = current_time;
				}
			}
		}

		k_mutex_unlock(&execution_registry->registry_mutex);
	}
}
#endif

/*******************************************************************************
 * Internal Interface Implementation
 ******************************************************************************/
int mcp_server_handle_request(mcp_server_ctx_t ctx, struct mcp_request_data *request,
			      enum mcp_method *method,
			      struct mcp_transport_binding **client_binding)
{
	int ret;
	struct mcp_queue_msg msg;
	struct mcp_client_context *client = NULL;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if ((server == NULL) || (request == NULL) || (request->json_data == NULL) ||
	    (method == NULL) || (client_binding == NULL)) {
		LOG_ERR("Invalid parameters passed to mcp_server_handle_request");
		return -EINVAL;
	}

	struct mcp_message *parsed_msg =
		(struct mcp_message *)mcp_alloc(sizeof(struct mcp_message));
	if (parsed_msg == NULL) {
		LOG_ERR("Failed to allocate memory for response");
		return -ENOMEM;
	}

	ret = mcp_json_parse_message(request->json_data, request->json_len, parsed_msg);
	if (ret != 0) {
		LOG_ERR("Failed to parse JSON request: %d", ret);
		mcp_free(parsed_msg);
		return ret;
	}

	*method = parsed_msg->method;
	LOG_WRN("Request method: %d", parsed_msg->method);

	switch (parsed_msg->method) {
	case MCP_METHOD_INITIALIZE:
		/* We want to handle the initialize request directly */
		ret = handle_initialize_request(server, parsed_msg, request->callback, &client);
		mcp_free(parsed_msg);
		break;
	case MCP_METHOD_PING:
		client = get_client(server, request->client_id_hint);
		if (client == NULL) {
			LOG_ERR("Can't find client with id %u", request->client_id_hint);
			ret = -ENOENT;
			mcp_free(parsed_msg);
			break;
		}
		ret = handle_ping_request(server, request->client_id_hint, parsed_msg);
		mcp_free(parsed_msg);
		break;
	case MCP_METHOD_TOOLS_LIST:
	case MCP_METHOD_TOOLS_CALL:
	case MCP_METHOD_NOTIF_INITIALIZED:
	case MCP_METHOD_NOTIF_CANCELLED:
		client = get_client(server, request->client_id_hint);
		if (client == NULL) {
			LOG_ERR("Can't find client with id %u", request->client_id_hint);
			ret = -ENOENT;
			mcp_free(parsed_msg);
			break;
		}

		msg.data = (void *)parsed_msg;
		msg.client_id = client->client_id;

		/* Parsed messge is now owned by the queue */
		ret = k_msgq_put(&server->request_queue, &msg, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("Failed to queue request message: %d", ret);
			mcp_free(parsed_msg);
		}

		break;
	case MCP_METHOD_UNKNOWN:
		client = get_client(server, request->client_id_hint);
		if (client == NULL) {
			LOG_ERR("Can't find client with id %u", request->client_id_hint);
			ret = -ENOENT;
			mcp_free(parsed_msg);
			break;
		}
		ret = send_error_response(server, parsed_msg->id, request->client_id_hint, MCP_ERR_METHOD_NOT_FOUND, "Method not found");
		mcp_free(parsed_msg);
		break;
	default:
		LOG_WRN("Request not recognized. Dropping.");
		ret = -ENOTSUP;
		break;
	}

	if (client != NULL) {
		*client_binding = &client->transport_binding;
	}

	return ret;
}

struct mcp_transport_binding *mcp_server_get_client_binding(mcp_server_ctx_t ctx,
							    uint32_t client_id)
{
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	struct mcp_client_context *client;

	if (server == NULL) {
		LOG_ERR("Invalid server");
		return NULL;
	}

	client = get_client(server, client_id);
	if (client == NULL) {
		LOG_ERR("Can't find client with id %u", client_id);
		return NULL;
	}

	return &client->transport_binding;
}
/*******************************************************************************
 * API Implementation
 ******************************************************************************/
mcp_server_ctx_t mcp_server_init(void)
{
	int ret;

	LOG_INF("Initializing MCP Server");

	struct mcp_server_ctx *server_ctx = allocate_mcp_server_context();
	if (server_ctx == NULL) {
		LOG_ERR("No available server contexts");
		return NULL;
	}

	k_msgq_init(&server_ctx->request_queue, server_ctx->request_queue_buffers,
		    sizeof(struct mcp_queue_msg), CONFIG_MCP_REQUEST_QUEUE_SIZE);

	ret = k_mutex_init(&server_ctx->client_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init client mutex: %d", ret);
		return NULL;
	}

	ret = k_mutex_init(&server_ctx->tool_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init tool mutex: %d", ret);
		return NULL;
	}

	ret = k_mutex_init(&server_ctx->execution_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init tool mutex: %d", ret);
		return NULL;
	}

	server_ctx->in_use = true;

	LOG_INF("MCP Server initialized");
	return (mcp_server_ctx_t)server_ctx;
}

int mcp_server_start(mcp_server_ctx_t ctx)
{
	k_tid_t tid;
	uint32_t thread_stack_idx;
	int ret;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	LOG_INF("Starting MCP Server");

	for (int i = 0; i < ARRAY_SIZE(server->request_workers); i++) {
		thread_stack_idx = (server->idx * CONFIG_MCP_REQUEST_WORKERS) + i;
		tid = k_thread_create(
			&server->request_workers[i], mcp_request_worker_stacks[thread_stack_idx],
			K_THREAD_STACK_SIZEOF(mcp_request_worker_stacks[thread_stack_idx]),
			mcp_request_worker, server, INT_TO_POINTER(i), NULL,
			K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);
		if (tid == NULL) {
			LOG_ERR("Failed to create request worker %d", i);
			return -ENOMEM;
		}

		ret = k_thread_name_set(&server->request_workers[i], "mcp_req_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set thread name: %d", ret);
		}
	}

#ifdef CONFIG_MCP_HEALTH_MONITOR
	tid = k_thread_create(&server->health_monitor_thread, mcp_health_monitor_stack[server->idx],
			      K_THREAD_STACK_SIZEOF(mcp_health_monitor_stack[server->idx]),
			      mcp_health_monitor_worker, server, NULL, NULL,
			      K_PRIO_COOP(MCP_WORKER_PRIORITY + 1), 0, K_NO_WAIT);
	if (tid == NULL) {
		LOG_ERR("Failed to create health monitor thread");
		return -ENOMEM;
	}

	ret = k_thread_name_set(&server->health_monitor_thread, "mcp_health_mon");
	if (ret != 0) {
		LOG_WRN("Failed to set health monitor thread name: %d", ret);
	}
	LOG_INF("MCP server health monitor enabled");
#endif

	LOG_INF("MCP Server started: %d request, %d response workers", CONFIG_MCP_REQUEST_WORKERS,
		CONFIG_MCP_RESPONSE_WORKERS);
	return 0;
}

int mcp_server_submit_tool_message(mcp_server_ctx_t ctx, const struct mcp_user_message *app_msg,
				   uint32_t execution_token)
{
	int ret;
	uint32_t request_id;
	uint32_t client_id;
	struct mcp_result_tools_call *response_data;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	struct mcp_execution_registry *execution_registry = &server->execution_registry;
	struct mcp_client_registry *client_registry = &server->client_registry;

	if ((app_msg == NULL) ||
	    ((app_msg->data == NULL) && (app_msg->type != MCP_USR_TOOL_CANCEL_ACK) &&
	     (app_msg->type != MCP_USR_TOOL_PING))) {
		LOG_ERR("Invalid user message");
		return -EINVAL;
	}

	if (execution_token == 0) {
		LOG_ERR("Invalid execution token");
		return -EINVAL;
	}

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	struct mcp_execution_context *execution_ctx =
		get_execution_context(server, execution_token);
	if (execution_ctx == NULL) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->registry_mutex);
		return -ENOENT;
	}

	request_id = execution_ctx->request_id;
	client_id = execution_ctx->client_id;

	struct mcp_client_context *client = get_client(server, client_id);

	bool is_execution_canceled = (execution_ctx->execution_state == MCP_EXEC_CANCELED);

	if (is_execution_canceled) {
		if (app_msg->type == MCP_USR_TOOL_CANCEL_ACK) {
			execution_ctx->execution_state = MCP_EXEC_FINISHED;
		} else if (app_msg->type == MCP_USR_TOOL_RESPONSE) {
			execution_ctx->execution_state = MCP_EXEC_FINISHED;
			LOG_WRN("Execution canceled, tool message will be dropped.");
		}
		k_mutex_unlock(&execution_registry->registry_mutex);
	} else {
		execution_ctx->last_message_timestamp = k_uptime_get();
		k_mutex_unlock(&execution_registry->registry_mutex);

		switch (app_msg->type) {
			/* Result is passed as a complete JSON string that gets attached to the full
			 * JSON RPC response inside the Transport layer
			 */
		case MCP_USR_TOOL_PING:
			return 0;
		case MCP_USR_TOOL_RESPONSE:

			if (client == NULL) {
				LOG_ERR("Client context not found for client_id: %u", client_id);
				return -ENOENT;
			}

			response_data = (struct mcp_result_tools_call *)mcp_alloc(
				sizeof(struct mcp_result_tools_call));
			if (response_data == NULL) {
				LOG_ERR("Failed to allocate memory for response");
				return -ENOMEM;
			}

			strncpy((char *)response_data->content.items[0].text, (char *)app_msg->data,
				sizeof(response_data->content.items[0].text) - 1);
			response_data->content.items[0]
				.text[sizeof(response_data->content.items[0].text) - 1] = '\0';
			response_data->content.count = 1;
			response_data->content.items[0].type = MCP_CONTENT_TEXT;

			execution_ctx->execution_state = MCP_EXEC_FINISHED;
			break;
		default:
			LOG_ERR("Unsupported application message type: %u", app_msg->type);
			return -EINVAL;
		}

		/* Allocate buffer for serialization */
		uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
		if (json_buffer == NULL) {
			LOG_ERR("Failed to allocate buffer, dropping message");
			mcp_free(response_data);
			return -ENOMEM;
		}

		/* Serialize response to JSON */
		ret = mcp_json_serialize_tools_call_result((char *)json_buffer,
							   CONFIG_MCP_MAX_MESSAGE_SIZE, request_id,
							   response_data);
		if (ret <= 0) {
			LOG_ERR("Failed to serialize response: %d", ret);
			mcp_free(response_data);
			mcp_free(json_buffer);
			return ret;
		}

		ret = client->transport_binding.ops->send(&client->transport_binding, client_id,
							  json_buffer, ret);
		if (ret) {
			LOG_ERR("Failed to tool response");
			mcp_free(response_data);
			mcp_free(json_buffer);
			return -EIO;
		}

		mcp_free(response_data);
	}

	if ((app_msg->type == MCP_USR_TOOL_RESPONSE) ||
	    (app_msg->type == MCP_USR_TOOL_CANCEL_ACK)) {
		ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry mutex: %d. Client registry is "
				"broken.",
				ret);
			goto skip_client_cleanup;
		}

		if (client == NULL) {
			if (is_execution_canceled) {
				LOG_DBG("Execution canceled, client was already cleaned up.");
			} else {
				LOG_ERR("Failed to find client index in the client registry. "
					"Client "
					"registry is broken.");
			}
			k_mutex_unlock(&client_registry->registry_mutex);
			goto skip_client_cleanup;
		}

		uint32_t *request = get_request(client, request_id);
		if (request == NULL) {
			LOG_ERR("Failed to find request index in client's active requests. Client "
				"registry is broken.");
			k_mutex_unlock(&client_registry->registry_mutex);
			goto skip_client_cleanup;
		}

		*request = 0;
		client->active_request_count--;

		k_mutex_unlock(&client_registry->registry_mutex);

skip_client_cleanup:
		ret = remove_execution_context(server, execution_ctx);
		if (ret != 0) {
			LOG_ERR("Failed to destroy execution context: %d. Execution registry is "
				"broken. Message was submitted successfully.",
				ret);
			return ret;
		}
	}

	return 0;
}

int mcp_server_add_tool(mcp_server_ctx_t ctx, const struct mcp_tool_record *tool_record)
{
	int ret;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if ((tool_record == NULL) || (tool_record->metadata.name[0] == '\0') ||
	    (tool_record->callback == NULL)) {
		LOG_ERR("Invalid tool record");
		return -EINVAL;
	}

	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	struct mcp_tool_record *tool = get_tool(server, tool_record->metadata.name);
	if (tool != NULL) {
		LOG_ERR("Tool '%s' already exists", tool_record->metadata.name);
		k_mutex_unlock(&tool_registry->registry_mutex);
		return -EEXIST;
	}

	struct mcp_tool_record *new_tool = add_tool(server, tool_record);
	if (new_tool == NULL) {
		LOG_ERR("Tool registry full");
		k_mutex_unlock(&tool_registry->registry_mutex);
		return -ENOSPC;
	}

	LOG_INF("Tool '%s' registered", tool_record->metadata.name);

	k_mutex_unlock(&tool_registry->registry_mutex);
	return 0;
}

int mcp_server_remove_tool(mcp_server_ctx_t ctx, const char *tool_name)
{
	int ret;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if ((tool_name == NULL) || (tool_name[0] == '\0')) {
		LOG_ERR("Invalid tool name");
		return -EINVAL;
	}

	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	struct mcp_tool_record *tool = get_tool(server, tool_name);
	if (tool == NULL) {
		k_mutex_unlock(&tool_registry->registry_mutex);
		LOG_ERR("Tool '%s' not found", tool_name);
		return -ENOENT;
	}

	memset(tool, 0, sizeof(struct mcp_tool_record));
	tool_registry->tool_count--;
	LOG_INF("Tool '%s' removed", tool_name);

	k_mutex_unlock(&tool_registry->registry_mutex);
	return 0;
}

int mcp_server_is_execution_canceled(mcp_server_ctx_t ctx, uint32_t execution_token,
				     bool *is_canceled)
{
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if (is_canceled == NULL) {
		LOG_ERR("Invalid is_canceled pointer");
		return -EINVAL;
	}

	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	int ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);

	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		*is_canceled = false;
		return ret;
	}

	struct mcp_execution_context *execution_ctx =
		get_execution_context(server, execution_token);

	if (execution_ctx == NULL) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->registry_mutex);
		*is_canceled = false;
		return -ENOENT;
	}

	*is_canceled = (execution_ctx->execution_state == MCP_EXEC_CANCELED);

	k_mutex_unlock(&execution_registry->registry_mutex);

	return 0;
}
