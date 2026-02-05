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

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define MCP_WORKER_PRIORITY  7
#define MCP_MAX_REQUESTS     (CONFIG_MCP_MAX_CLIENTS * CONFIG_MCP_MAX_CLIENT_REQUESTS)
#define MCP_SERVER_VERSION   "1.0.0"
#define MCP_PROTOCOL_VERSION "2025-11-25"

/* Lifecycle monitoring of a client's session */
enum mcp_lifecycle_state {
	MCP_LIFECYCLE_DEINITIALIZED = 0,
	MCP_LIFECYCLE_NEW,
	MCP_LIFECYCLE_INITIALIZING,
	MCP_LIFECYCLE_INITIALIZED
};

/* Lifecycle monitoring of a tool execution */
enum mcp_execution_state {
	MCP_EXEC_ACTIVE,
	MCP_EXEC_CANCELED,
	MCP_EXEC_FINISHED
};

struct mcp_client_context {
	enum mcp_lifecycle_state lifecycle_state;
	uint8_t active_request_count;
	atomic_t refcount;
	int64_t last_message_timestamp;
	struct mcp_transport_binding *binding;
};

/* Struct holding the pointer to a client's request's data */
struct mcp_queue_msg {
	struct mcp_client_context *client;
	uint32_t transport_msg_id;
	void *data;
};

/* Registry holding the tools added by the user application to the server */
struct mcp_tool_registry {
	struct mcp_tool_record tools[CONFIG_MCP_MAX_TOOLS];
	struct k_mutex mutex;
	uint8_t tool_count;
};

/* Context for a tool execution */
struct mcp_execution_context {
	uint32_t execution_token;
	struct mcp_request_id request_id;
	uint32_t transport_msg_id;
	struct mcp_client_context *client;
	struct mcp_tool_record *tool;
	k_tid_t worker_id;
	int64_t start_timestamp;
	int64_t cancel_timestamp;
	int64_t last_message_timestamp;
	enum mcp_execution_state execution_state;
};

struct mcp_execution_registry {
	struct mcp_execution_context executions[MCP_MAX_REQUESTS];
	struct k_mutex mutex;
};

struct mcp_client_registry {
	struct mcp_client_context clients[CONFIG_MCP_MAX_CLIENTS];
	struct k_mutex mutex;
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
	char request_queue_storage[MCP_MAX_REQUESTS * sizeof(struct mcp_queue_msg)];
	struct mcp_tool_registry tool_registry;
	struct mcp_execution_registry execution_registry;
#ifdef CONFIG_MCP_HEALTH_MONITOR
	struct k_thread health_monitor_thread;
#endif
};

/*******************************************************************************
 * Variables
 ******************************************************************************/
static struct mcp_server_ctx mcp_servers[CONFIG_MCP_SERVER_COUNT];

K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks,
			    CONFIG_MCP_REQUEST_WORKERS *CONFIG_MCP_SERVER_COUNT,
			    CONFIG_MCP_REQUEST_WORKER_STACK_SIZE);
#ifdef CONFIG_MCP_HEALTH_MONITOR
K_THREAD_STACK_ARRAY_DEFINE(mcp_health_monitor_stack, CONFIG_MCP_SERVER_COUNT,
			    CONFIG_MCP_HEALTH_MONITOR_STACK_SIZE);
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
 * NOTE: Functions with _locked suffix assume the caller holds client_registry.mutex
 ******************************************************************************/
/* Must be called with client_registry.mutex held */
static struct mcp_client_context *
get_client_by_binding_locked(struct mcp_server_ctx *server,
			     const struct mcp_transport_binding *binding)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].binding == binding) {
			return &client_registry->clients[i];
		}
	}

	return NULL;
}

/* Must be called with client_registry.mutex held */
static struct mcp_client_context *client_get_locked(struct mcp_client_context *client)
{
	if (client == NULL || client->lifecycle_state == MCP_LIFECYCLE_DEINITIALIZED) {
		return NULL;
	}
	atomic_inc(&client->refcount);
	return client;
}

/* Can be called without lock */
static void client_put(struct mcp_server_ctx *server, struct mcp_client_context *client)
{
	if (client == NULL) {
		return;
	}

	if (atomic_dec(&client->refcount) == 1) {
		/* Last reference. Safe to cleanup */
		client->binding->ops->disconnect(client->binding);
		memset(client, 0, sizeof(struct mcp_client_context));
		client->lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;
	}
}

/* Must be called with client_registry.mutex held */
static struct mcp_client_context *add_client_locked(struct mcp_server_ctx *server,
						    struct mcp_transport_binding *binding)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].lifecycle_state == MCP_LIFECYCLE_DEINITIALIZED) {
			client_registry->clients[i].lifecycle_state = MCP_LIFECYCLE_NEW;
			client_registry->clients[i].active_request_count = 0;
			client_registry->clients[i].binding = binding;
			atomic_set(&client_registry->clients[i].refcount, 1);
			return &client_registry->clients[i];
		}
	}

	return NULL;
}

/* Must be called with client_registry.mutex held */
static void remove_client_locked(struct mcp_server_ctx *server, struct mcp_client_context *client)
{
	/* Mark as deinitialized. Cleanup happens when refcount reaches 0 */
	client->lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;

	/* Drop the initial reference */
	k_mutex_unlock(&server->client_registry.mutex);
	client_put(server, client);
	k_mutex_lock(&server->client_registry.mutex, K_FOREVER);
}

/*******************************************************************************
 * Execution Context Helper Functions
 * NOTE: All these functions assume the caller holds execution_registry.mutex
 ******************************************************************************/
static uint32_t generate_execution_token(uint32_t msg_id)
{
	/* Mocking the generation for the 1st phase of development. Security phase
	 * will replace this with UUID generation to make token guessing harder.
	 */
	return msg_id;
}

/* Must be called with execution_registry.mutex held */
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

/* Must be called with execution_registry.mutex held */
static struct mcp_execution_context *add_execution_context(struct mcp_server_ctx *server,
							   struct mcp_client_context *client,
							   struct mcp_request_id *request_id,
							   uint32_t msg_id)
{
	struct mcp_execution_context *context = NULL;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;
	uint32_t execution_token = generate_execution_token(msg_id);

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		context = &execution_registry->executions[i];
		if (context->execution_token == 0) {
			context->execution_token = execution_token;
			context->request_id = *request_id;
			context->transport_msg_id = msg_id;
			context->client = client;
			context->worker_id = k_current_get();
			context->start_timestamp = k_uptime_get();
			context->cancel_timestamp = 0;
			context->last_message_timestamp = k_uptime_get();
			context->execution_state = MCP_EXEC_ACTIVE;
			return context;
		}
	}

	LOG_ERR("Execution registry full");
	return NULL;
}

/* Must be called with execution_registry.mutex held */
static void remove_execution_context(struct mcp_server_ctx *server,
				     struct mcp_execution_context *execution_context)
{
	memset(execution_context, 0, sizeof(struct mcp_execution_context));
}

/*******************************************************************************
 * Tools Context Helper Functions
 * NOTE: All these functions assume the caller holds tool_registry.mutex
 ******************************************************************************/
/* Must be called with tool_registry.mutex held */
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

/* Must be called with tool_registry.mutex held */
static struct mcp_tool_record *add_tool(struct mcp_server_ctx *server,
					const struct mcp_tool_record *tool_info)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	for (int i = 0; i < ARRAY_SIZE(tool_registry->tools); i++) {
		if (tool_registry->tools[i].metadata.name[0] == '\0') {
			memcpy(&tool_registry->tools[i], tool_info, sizeof(struct mcp_tool_record));
			tool_registry->tool_count++;
			tool_registry->tools[i].activity_counter = 0;
			return &tool_registry->tools[i];
		}
	}

	return NULL;
}

/* Must be called with tool_registry.mutex held */
static int copy_tool_metadata_to_response(struct mcp_server_ctx *server,
					  struct mcp_result_tools_list *response_data)
{
	struct mcp_tool_metadata *tool_info;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;
	char *buf = response_data->tools_json;
	size_t buf_size = sizeof(response_data->tools_json);
	size_t offset = 0;
	int ret;

	/* Start with opening bracket */
	ret = snprintk(buf + offset, buf_size - offset, "[");
	if (ret < 0 || (size_t)ret >= buf_size - offset) {
		LOG_ERR("Buffer overflow adding opening bracket");
		return -ENOMEM;
	}
	offset += ret;

	for (int i = 0; i < tool_registry->tool_count; i++) {
		tool_info = &tool_registry->tools[i].metadata;

		ret = snprintk(buf + offset, buf_size - offset,
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

		if (ret < 0 || (size_t)ret >= buf_size - offset) {
			LOG_ERR("Buffer overflow adding tool %d", i);
			return -ENOMEM;
		}

		offset += ret;
	}

	/* Add closing bracket */
	ret = snprintk(buf + offset, buf_size - offset, "]");
	if (ret < 0 || (size_t)ret >= buf_size - offset) {
		LOG_ERR("Buffer overflow adding closing bracket");
		return -ENOMEM;
	}

	return 0;
}

/*******************************************************************************
 * Request/Response Handling Functions
 ******************************************************************************/
static int send_error_response(struct mcp_server_ctx *server, struct mcp_client_context *client,
			       struct mcp_request_id *request_id, int32_t error_code,
			       const char *error_message, uint32_t msg_id)
{
	struct mcp_error *error_response;
	int ret;

	error_response = (struct mcp_error *)mcp_alloc(sizeof(struct mcp_error));
	if (error_response == NULL) {
		LOG_ERR("Failed to allocate error response");
		return -ENOMEM;
	}

	mcp_safe_strcpy(error_response->message, sizeof(error_response->message), error_message);

	error_response->code = error_code;
	error_response->has_data = false;

	/* Allocate buffer for serialization */
	char *json_buffer = (char *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(error_response);
		return -ENOMEM;
	}

	ret = mcp_json_serialize_error(json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE, request_id,
				       error_response);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(error_response);
		mcp_free(json_buffer);
		return ret;
	}

	struct mcp_transport_message tx_msg = {.binding = client->binding,
					       .msg_id = msg_id,
					       .json_data = json_buffer,
					       .json_len = ret};

	ret = client->binding->ops->send(&tx_msg);
	if (ret) {
		LOG_ERR("Failed to send error response");
		mcp_free(error_response);
		mcp_free(json_buffer);
		return ret;
	}

	mcp_free(error_response);
	return 0;
}

static int handle_initialize_request(struct mcp_server_ctx *server, struct mcp_message *request,
				     struct mcp_transport_binding *binding, uint32_t msg_id)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_client_context *new_client;
	struct mcp_result_initialize *response_data = NULL;
	uint8_t *json_buffer = NULL;

	if (strcmp(request->req.u.initialize.protocol_version, MCP_PROTOCOL_VERSION) != 0) {
		LOG_WRN("Protocol version mismatch: %s",
			request->req.u.initialize.protocol_version);
		return -EINVAL;
	}

	/* Lock client registry and add new client */
	ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	new_client = add_client_locked(server, binding);
	if (new_client == NULL) {
		LOG_ERR("Client registry full");
		k_mutex_unlock(&client_registry->mutex);
		return -ENOMEM;
	}

	new_client->lifecycle_state = MCP_LIFECYCLE_INITIALIZING;

	client_get_locked(new_client);
	k_mutex_unlock(&client_registry->mutex);

	/* Allocate and prepare response */
	response_data =
		(struct mcp_result_initialize *)mcp_alloc(sizeof(struct mcp_result_initialize));
	if (response_data == NULL) {
		LOG_ERR("Failed to allocate response");
		ret = -ENOMEM;
		goto cleanup;
	}

	mcp_safe_strcpy(response_data->server_version, sizeof(response_data->server_version),
			CONFIG_MCP_SERVER_INFO_VERSION);
	mcp_safe_strcpy(response_data->server_name, sizeof(response_data->server_name),
			CONFIG_MCP_SERVER_INFO_NAME);
	mcp_safe_strcpy(response_data->protocol_version, sizeof(response_data->protocol_version),
			MCP_PROTOCOL_VERSION);
	mcp_safe_strcpy(response_data->capabilities_json, sizeof(response_data->capabilities_json),
			"{\"tools\":{\"listChanged\":false}}");
	response_data->has_capabilities = true;

	/* Allocate buffer for serialization */
	json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_initialize_result((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
						   &request->id, response_data);
	if (ret < 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		goto cleanup;
	}

	struct mcp_transport_message tx_msg = {.binding = new_client->binding,
					       .msg_id = msg_id,
					       .json_data = json_buffer,
					       .json_len = ret};

	ret = new_client->binding->ops->send(&tx_msg);
	if (ret) {
		LOG_ERR("Failed to send initialize response %d", ret);
		goto cleanup;
	}

	mcp_free(response_data);
	client_put(server, new_client);
	return 0;

cleanup:
	if (json_buffer) {
		mcp_free(json_buffer);
	}
	if (response_data) {
		mcp_free(response_data);
	}

	/* Remove client on failure */
	if (k_mutex_lock(&client_registry->mutex, K_FOREVER) == 0) {
		remove_client_locked(server, new_client);
		k_mutex_unlock(&client_registry->mutex);
	}

	client_put(server, new_client);
	return ret;
}

static int handle_tools_list_request(struct mcp_server_ctx *server,
				     struct mcp_client_context *client, struct mcp_message *request,
				     uint32_t msg_id)
{
	struct mcp_result_tools_list *response_data = NULL;
	uint8_t *json_buffer = NULL;
	int ret;

	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	LOG_DBG("Processing tools list request");

	ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %p", client);
		k_mutex_unlock(&client_registry->mutex);
		return -EACCES;
	}

	client_get_locked(client);
	k_mutex_unlock(&client_registry->mutex);

	/* Allocate response structure */
	response_data =
		(struct mcp_result_tools_list *)mcp_alloc(sizeof(struct mcp_result_tools_list));
	if (response_data == NULL) {
		LOG_ERR("Failed to allocate response");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Copy tool metadata while holding tool registry lock */
	ret = k_mutex_lock(&tool_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry");
		goto cleanup;
	}

	ret = copy_tool_metadata_to_response(server, response_data);
	k_mutex_unlock(&tool_registry->mutex);

	if (ret != 0) {
		goto cleanup;
	}

	/* Allocate buffer for serialization */
	json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_tools_list_result((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
						   &request->id, response_data);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		goto cleanup;
	}

	struct mcp_transport_message tx_msg = {.binding = client->binding,
					       .msg_id = msg_id,
					       .json_data = json_buffer,
					       .json_len = ret};

	ret = client->binding->ops->send(&tx_msg);
	if (ret) {
		LOG_ERR("Failed to send tools list response");
		goto cleanup;
	}

	mcp_free(response_data);
	client_put(server, client);
	return 0;

cleanup:
	if (json_buffer) {
		mcp_free(json_buffer);
	}

	if (response_data) {
		mcp_free(response_data);
	}

	client_put(server, client);
	return ret;
}

static int handle_tools_call_request(struct mcp_server_ctx *server,
				     struct mcp_client_context *client, struct mcp_message *request,
				     uint32_t msg_id)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;
	struct mcp_tool_record *tool;
	struct mcp_execution_context *exec_ctx;

	LOG_DBG("Processing tools call request");

	/* Check client state, increment active request count, and acquire reference */
	ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %p", client);
		k_mutex_unlock(&client_registry->mutex);
		return -EACCES;
	}

	if (client->active_request_count >= CONFIG_MCP_MAX_CLIENT_REQUESTS) {
		LOG_DBG("Client (%p) has reached maximum active requests", client);
		k_mutex_unlock(&client_registry->mutex);
		return -EBUSY;
	}

	client->active_request_count++;
	client_get_locked(client);
	k_mutex_unlock(&client_registry->mutex);

	/* Look up tool */
	ret = k_mutex_lock(&tool_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		goto cleanup_active_request;
	}

	tool = get_tool(server, request->req.u.tools_call.name);
	tool->activity_counter++;
	k_mutex_unlock(&tool_registry->mutex);

	if (tool == NULL) {
		LOG_ERR("Tool '%s' not found", request->req.u.tools_call.name);
		ret = -ENOENT;
		goto cleanup_active_request;
	}

	/* Create execution context */
	ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		goto cleanup_active_request;
	}

	exec_ctx = add_execution_context(server, client, &request->id, msg_id);
	exec_ctx->tool = tool;
	k_mutex_unlock(&execution_registry->mutex);

	if (exec_ctx == NULL) {
		LOG_ERR("Failed to create execution context");
		ret = -ENOMEM;
		goto cleanup_active_request;
	}

	/* Call the tool callback */
	ret = tool->callback(request->req.u.tools_call.arguments_json, exec_ctx->execution_token);
	if (ret != 0) {
		LOG_ERR("Tool callback failed: %d", ret);

		/* Remove execution context on failure */
		if (k_mutex_lock(&execution_registry->mutex, K_FOREVER) == 0) {
			remove_execution_context(server, exec_ctx);
			k_mutex_unlock(&execution_registry->mutex);
		}

		/* Decrement tool activity counter on failure */
		if (k_mutex_lock(&tool_registry->mutex, K_FOREVER) == 0) {
			tool->activity_counter--;
			k_mutex_unlock(&tool_registry->mutex);
		}

		goto cleanup_active_request;
	}

	client_put(server, client);
	return 0;

cleanup_active_request:
	if (k_mutex_lock(&client_registry->mutex, K_FOREVER) == 0) {
		client->active_request_count--;
		k_mutex_unlock(&client_registry->mutex);
	}
	client_put(server, client);
	return ret;
}

static int handle_notification(struct mcp_server_ctx *server, struct mcp_client_context *client,
			       struct mcp_message *notification)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	LOG_DBG("Processing notification");

	ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry");
		return ret;
	}

	if (client_get_locked(client) == NULL) {
		k_mutex_unlock(&client_registry->mutex);
		return -ENOENT;
	}

	switch (notification->method) {
	case MCP_METHOD_NOTIF_INITIALIZED:
		/* State transition: INITIALIZING -> INITIALIZED */
		if (client->lifecycle_state == MCP_LIFECYCLE_INITIALIZING) {
			client->lifecycle_state = MCP_LIFECYCLE_INITIALIZED;
		} else {
			LOG_ERR("Invalid state transition for client %p", client);
			k_mutex_unlock(&client_registry->mutex);
			client_put(server, client);
			return -EPERM;
		}
		break;
	case MCP_METHOD_NOTIF_CANCELLED:
		/*TODO: Implement */
		break;
	default:
		LOG_ERR("Unknown notification method %u", notification->method);
		k_mutex_unlock(&client_registry->mutex);
		client_put(server, client);
		return -EINVAL;
	}

	k_mutex_unlock(&client_registry->mutex);
	client_put(server, client);
	return 0;
}

static int handle_ping_request(struct mcp_server_ctx *server, struct mcp_client_context *client,
			       struct mcp_message *request, uint32_t msg_id)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;
	uint8_t *json_buffer = NULL;

	LOG_DBG("Processing ping request");

	/* Check client state and acquire reference */
	ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	if (client->lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %p", client);
		k_mutex_unlock(&client_registry->mutex);
		return -EACCES;
	}

	client_get_locked(client);
	k_mutex_unlock(&client_registry->mutex);

	/* Allocate buffer for serialization */
	json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
	if (json_buffer == NULL) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_ping_result((char *)json_buffer, CONFIG_MCP_MAX_MESSAGE_SIZE,
					     &request->id, NULL);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		goto cleanup;
	}

	struct mcp_transport_message tx_msg = {.binding = client->binding,
					       .msg_id = msg_id,
					       .json_data = json_buffer,
					       .json_len = ret};

	ret = client->binding->ops->send(&tx_msg);
	if (ret) {
		LOG_ERR("Failed to send ping response");
		goto cleanup;
	}

	client_put(server, client);
	return 0;

cleanup:
	if (json_buffer) {
		mcp_free(json_buffer);
	}
	client_put(server, client);
	return ret;
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
			if (request.client) {
				client_put(server, request.client);
			}
			continue;
		}

		message = (struct mcp_message *)request.data;

		switch (message->method) {
		case MCP_METHOD_INITIALIZE:
			/* Handled immediately in mcp_server_handle_request */
			LOG_DBG("Should never reach here");
			ret = 0;
			break;
		case MCP_METHOD_PING:
			ret = handle_ping_request(server, request.client, message,
						  request.transport_msg_id);
			break;
		case MCP_METHOD_NOTIF_INITIALIZED:
			ret = handle_notification(server, request.client, message);
			break;
		case MCP_METHOD_TOOLS_LIST:
			ret = handle_tools_list_request(server, request.client, message,
							request.transport_msg_id);
			break;
		case MCP_METHOD_TOOLS_CALL:
			ret = handle_tools_call_request(server, request.client, message,
							request.transport_msg_id);
			break;
		case MCP_METHOD_NOTIF_CANCELLED:
			/* TODO: Implement. Ignore for now */
			ret = 0;
			break;
		default:
			/* Should never get here. Requests are validated in
			 * mcp_server_handle_request */
			LOG_ERR("Unknown request");
			mcp_free(request.data);
			client_put(server, request.client);
			continue;
		}

		if ((message->kind != MCP_MSG_NOTIFICATION) && (ret != 0)) {
			switch (ret) {
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
			case -EACCES:
				error_code = MCP_ERR_INVALID_PARAMS;
				error_message = "Client not initialized";
				break;
			case -EBUSY:
				error_code = MCP_ERR_BUSY;
				error_message = "Client is busy";
				break;
			default:
				error_code = MCP_ERR_INTERNAL_ERROR;
				error_message = "Internal server error";
				break;
			}
			send_error_response(server, request.client, &message->id, error_code,
					    error_message, request.transport_msg_id);
		}

		mcp_free(request.data);
		/* Release reference acquired when queuing */
		client_put(server, request.client);
	}
}

#ifdef CONFIG_MCP_HEALTH_MONITOR
static void mcp_health_monitor_worker(void *ctx, void *arg2, void *arg3)
{
	int ret;
	int64_t current_time;
	int64_t execution_duration;
	int64_t idle_duration;
	int64_t cancel_duration;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;
	struct mcp_client_registry *client_registry = &server->client_registry;

	while (1) {
		k_sleep(K_MSEC(CONFIG_MCP_HEALTH_CHECK_INTERVAL_MS));

		current_time = k_uptime_get();

		/* Check execution contexts */
		ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);
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
					LOG_ERR("Execution token %u exceeded cancellation "
						"timeout (%lld ms). Client: %p, Worker ID %u",
						context->execution_token, cancel_duration,
						context->client, (uint32_t)context->worker_id);
					/* TODO: Clean up execution record? */
				}
				continue;
			}

			if (context->execution_state == MCP_EXEC_FINISHED) {
				continue;
			}

			execution_duration = current_time - context->start_timestamp;

			if (execution_duration > CONFIG_MCP_TOOL_EXEC_TIMEOUT_MS) {
				LOG_WRN("Execution token %u exceeded execution timeout "
					"(%lld ms). Client: %p, Worker ID %u",
					context->execution_token, execution_duration,
					context->client, (uint32_t)context->worker_id);
				/* TODO: Notify client? */
				context->execution_state = MCP_EXEC_CANCELED;
				context->cancel_timestamp = current_time;
				continue;
			}

			if (context->last_message_timestamp > 0) {
				idle_duration = current_time - context->last_message_timestamp;

				if (idle_duration > CONFIG_MCP_TOOL_IDLE_TIMEOUT_MS) {
					LOG_WRN("Execution token %u exceeded idle timeout "
						"(%lld ms). Client: %p, Worker ID %u",
						context->execution_token, idle_duration,
						context->client, (uint32_t)context->worker_id);
					/* TODO: Notify client? */
					context->execution_state = MCP_EXEC_CANCELED;
					context->cancel_timestamp = current_time;
				}
			}
		}

		k_mutex_unlock(&execution_registry->mutex);

		/* Check client contexts */
		ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry: %d", ret);
			continue;
		}

		for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
			struct mcp_client_context *client_context = &client_registry->clients[i];

			if (client_context->lifecycle_state == MCP_LIFECYCLE_DEINITIALIZED) {
				continue;
			}

			if (client_context->last_message_timestamp > 0) {
				idle_duration =
					current_time - client_context->last_message_timestamp;

				if (idle_duration > CONFIG_MCP_CLIENT_TIMEOUT_MS) {
					LOG_WRN("Client %p exceeded idle timeout (%lld ms). "
						"Marking as disconnected.",
						client_context, idle_duration);

					remove_client_locked(server, client_context);
				}
			}
		}
		k_mutex_unlock(&client_registry->mutex);
	}
}
#endif

/*******************************************************************************
 * Internal Interface Implementation
 ******************************************************************************/
int mcp_server_handle_request(mcp_server_ctx_t ctx, struct mcp_transport_message *request,
			      enum mcp_method *method)
{
	int ret;
	struct mcp_queue_msg msg;
	struct mcp_client_context *client = NULL;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	struct mcp_client_registry *client_registry = &server->client_registry;

	if ((server == NULL) || (request == NULL) || (request->json_data == NULL) ||
	    (method == NULL)) {
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
	LOG_DBG("Request method: %d", parsed_msg->method);

	switch (parsed_msg->method) {
	case MCP_METHOD_INITIALIZE:
		/* We want to handle the initialize request directly */
		ret = handle_initialize_request(server, parsed_msg, request->binding,
						request->msg_id);
		mcp_free(parsed_msg);
		break;
	case MCP_METHOD_PING:
	case MCP_METHOD_TOOLS_LIST:
	case MCP_METHOD_TOOLS_CALL:
	case MCP_METHOD_NOTIF_INITIALIZED:
	case MCP_METHOD_NOTIF_CANCELLED:
		ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry: %d", ret);
			mcp_free(parsed_msg);
			return ret;
		}

		client = get_client_by_binding_locked(server, request->binding);
		if (client == NULL) {
			LOG_ERR("Client does not exist");
			k_mutex_unlock(&client_registry->mutex);
			mcp_free(parsed_msg);
			ret = -ENOENT;
			break;
		}

		/* Acquire reference for queued message */
		client_get_locked(client);
		k_mutex_unlock(&client_registry->mutex);

		msg.data = (void *)parsed_msg;
		msg.client = client;
		msg.transport_msg_id = request->msg_id;

		/* Parsed message is now owned by the queue */
		ret = k_msgq_put(&server->request_queue, &msg, K_NO_WAIT);
		if (ret != 0) {
			LOG_ERR("Failed to queue request message: %d", ret);
			mcp_free(parsed_msg);
			client_put(server, client);
		}

		break;
	case MCP_METHOD_UNKNOWN:
		ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry: %d", ret);
			mcp_free(parsed_msg);
			return ret;
		}

		client = get_client_by_binding_locked(server, request->binding);
		if (client == NULL) {
			LOG_ERR("Client does not exist.");
			k_mutex_unlock(&client_registry->mutex);
			mcp_free(parsed_msg);
			ret = -ENOENT;
			break;
		}

		client_get_locked(client);
		k_mutex_unlock(&client_registry->mutex);

		ret = send_error_response(server, client, &parsed_msg->id, MCP_ERR_METHOD_NOT_FOUND,
					  "Method not found", request->msg_id);
		mcp_free(parsed_msg);
		client_put(server, client);
		break;
	default:
		LOG_WRN("Request not recognized. Dropping.");
		ret = -ENOTSUP;
		mcp_free(parsed_msg);
		break;
	}

	return ret;
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

	k_msgq_init(&server_ctx->request_queue, server_ctx->request_queue_storage,
		    sizeof(struct mcp_queue_msg), MCP_MAX_REQUESTS);

	ret = k_mutex_init(&server_ctx->client_registry.mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init client mutex: %d", ret);
		return NULL;
	}

	ret = k_mutex_init(&server_ctx->tool_registry.mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init tool mutex: %d", ret);
		return NULL;
	}

	ret = k_mutex_init(&server_ctx->execution_registry.mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init execution mutex: %d", ret);
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
#if CONFIG_THREAD_NAME
	int ret;
#endif
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

#if CONFIG_THREAD_NAME
		ret = k_thread_name_set(&server->request_workers[i], "mcp_req_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set thread name: %d", ret);
		}
#endif
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

#if CONFIG_THREAD_NAME
	ret = k_thread_name_set(&server->health_monitor_thread, "mcp_health_mon");
	if (ret != 0) {
		LOG_WRN("Failed to set health monitor thread name: %d", ret);
	}
#endif

	LOG_INF("MCP server health monitor enabled");
#endif

	LOG_INF("MCP Server started: %d request workers", CONFIG_MCP_REQUEST_WORKERS);
	return 0;
}

int mcp_server_submit_tool_message(mcp_server_ctx_t ctx, const struct mcp_tool_message *tool_msg,
				   uint32_t execution_token)
{
	int ret;
	struct mcp_request_id *request_id;
	struct mcp_result_tools_call *response_data = NULL;
	uint8_t *json_buffer = NULL;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (server == NULL) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	struct mcp_execution_registry *execution_registry = &server->execution_registry;
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	if ((tool_msg == NULL) ||
	    ((tool_msg->data == NULL) && (tool_msg->type != MCP_USR_TOOL_CANCEL_ACK) &&
	     (tool_msg->type != MCP_USR_TOOL_PING))) {
		LOG_ERR("Invalid user message");
		return -EINVAL;
	}

	if (execution_token == 0) {
		LOG_ERR("Invalid execution token");
		return -EINVAL;
	}

	/* Look up execution context and check state */
	ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	struct mcp_execution_context *execution_ctx =
		get_execution_context(server, execution_token);
	if (execution_ctx == NULL) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->mutex);
		return -ENOENT;
	}

	request_id = &execution_ctx->request_id;
	struct mcp_client_context *client = execution_ctx->client;

	bool is_execution_canceled = (execution_ctx->execution_state == MCP_EXEC_CANCELED);

	if (is_execution_canceled) {
		if (tool_msg->type == MCP_USR_TOOL_CANCEL_ACK) {
			execution_ctx->execution_state = MCP_EXEC_FINISHED;
		} else if (tool_msg->type == MCP_USR_TOOL_RESPONSE) {
			execution_ctx->execution_state = MCP_EXEC_FINISHED;
			LOG_WRN("Execution canceled, tool message will be dropped.");
		} else {
			LOG_DBG("Execution canceled, ignoring tool message type: %d",
				tool_msg->type);
		}
		k_mutex_unlock(&execution_registry->mutex);
	} else {
		execution_ctx->last_message_timestamp = k_uptime_get();
		k_mutex_unlock(&execution_registry->mutex);

		switch (tool_msg->type) {
		case MCP_USR_TOOL_PING:
			/* Ping from a tool signifying that a long execution is not frozen - no need
			 * for any kind of processing
			 */
			return 0;
		case MCP_USR_TOOL_RESPONSE:

			if (client == NULL) {
				LOG_ERR("Client context not found for client: %p", client);
				return -ENOENT;
			}

			response_data = (struct mcp_result_tools_call *)mcp_alloc(
				sizeof(struct mcp_result_tools_call));
			if (response_data == NULL) {
				LOG_ERR("Failed to allocate memory for response");
				return -ENOMEM;
			}

			mcp_safe_strcpy((char *)response_data->content.items[0].text,
					sizeof(response_data->content.items[0].text),
					(char *)tool_msg->data);
			response_data->content.count = 1;
			response_data->content.items[0].type = MCP_CONTENT_TEXT;
			response_data->is_error = tool_msg->is_error;

			/* Update execution state */
			ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to lock execution registry: %d", ret);
				mcp_free(response_data);
				return ret;
			}
			execution_ctx->execution_state = MCP_EXEC_FINISHED;
			k_mutex_unlock(&execution_registry->mutex);
			break;
		default:
			LOG_ERR("Unsupported application message type: %u", tool_msg->type);
			return -EINVAL;
		}

		/* Allocate buffer for serialization */
		json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_MAX_MESSAGE_SIZE);
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

		struct mcp_transport_message tx_msg = {.binding = client->binding,
						       .msg_id = execution_ctx->transport_msg_id,
						       .json_data = json_buffer,
						       .json_len = ret};

		ret = client->binding->ops->send(&tx_msg);
		if (ret) {
			LOG_ERR("Failed to send tool response");
			mcp_free(response_data);
			mcp_free(json_buffer);
			return ret;
		}

		mcp_free(response_data);
	}

	/* Clean up if this is a final message */
	if ((tool_msg->type == MCP_USR_TOOL_RESPONSE) ||
	    (tool_msg->type == MCP_USR_TOOL_CANCEL_ACK)) {
		/* Decrement active request count */
		ret = k_mutex_lock(&client_registry->mutex, K_FOREVER);
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
				LOG_ERR("Failed to find client in the client registry. "
					"Client registry is broken.");
			}
			k_mutex_unlock(&client_registry->mutex);
			goto skip_client_cleanup;
		}

		client->active_request_count--;
		k_mutex_unlock(&client_registry->mutex);

skip_client_cleanup:
		struct mcp_tool_record *temp_tool_ptr;
		/* Remove execution context */
		ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock execution registry: %d. Execution registry is "
				"broken. Message was submitted successfully.",
				ret);
			return ret;
		}
		temp_tool_ptr = execution_ctx->tool;
		remove_execution_context(server, execution_ctx);
		k_mutex_unlock(&execution_registry->mutex);

		/* Decrement tool activity counter */
		ret = k_mutex_lock(&tool_registry->mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock tool registry: %d. Tool registry is "
				"broken. Message was submitted successfully.",
				ret);
			return ret;
		}

		temp_tool_ptr->activity_counter--;
		k_mutex_unlock(&tool_registry->mutex);
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

	ret = k_mutex_lock(&tool_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	struct mcp_tool_record *tool = get_tool(server, tool_record->metadata.name);
	if (tool != NULL) {
		LOG_ERR("Tool '%s' already exists", tool_record->metadata.name);
		k_mutex_unlock(&tool_registry->mutex);
		return -EEXIST;
	}

	struct mcp_tool_record *new_tool = add_tool(server, tool_record);
	if (new_tool == NULL) {
		LOG_ERR("Tool registry full");
		k_mutex_unlock(&tool_registry->mutex);
		return -ENOSPC;
	}

	LOG_INF("Tool '%s' registered", tool_record->metadata.name);

	k_mutex_unlock(&tool_registry->mutex);
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

	ret = k_mutex_lock(&tool_registry->mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	struct mcp_tool_record *tool = get_tool(server, tool_name);
	if (tool == NULL) {
		k_mutex_unlock(&tool_registry->mutex);
		LOG_ERR("Tool '%s' not found", tool_name);
		return -ENOENT;
	}

	if (tool->activity_counter > 0) {
		k_mutex_unlock(&tool_registry->mutex);
		LOG_INF("Requested removal of a currently active tool '%s'", tool_name);
		return -EBUSY;
	}

	memset(tool, 0, sizeof(struct mcp_tool_record));
	tool_registry->tool_count--;
	LOG_INF("Tool '%s' removed", tool_name);

	k_mutex_unlock(&tool_registry->mutex);
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

	int ret = k_mutex_lock(&execution_registry->mutex, K_FOREVER);

	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		*is_canceled = false;
		return ret;
	}

	struct mcp_execution_context *execution_ctx =
		get_execution_context(server, execution_token);

	if (execution_ctx == NULL) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->mutex);
		*is_canceled = false;
		return -ENOENT;
	}

	*is_canceled = (execution_ctx->execution_state == MCP_EXEC_CANCELED);

	k_mutex_unlock(&execution_registry->mutex);

	return 0;
}
