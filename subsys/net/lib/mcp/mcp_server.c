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

#define MCP_WORKER_PRIORITY 7

enum mcp_lifecycle_state {
	MCP_LIFECYCLE_DEINITIALIZED = 0,
	MCP_LIFECYCLE_NEW,
	MCP_LIFECYCLE_INITIALIZING,
	MCP_LIFECYCLE_INITIALIZED,
	MCP_LIFECYCLE_DEINITIALIZING
};

struct mcp_client_context {
	uint32_t client_id;
	enum mcp_lifecycle_state lifecycle_state;
	uint32_t active_requests[CONFIG_HTTP_SERVER_MAX_STREAMS];
	uint8_t active_request_count;
};

struct mcp_client_registry {
	struct mcp_client_context clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex registry_mutex;
	uint8_t client_count;
};

/*
 * @brief MCP server context structure
 * @details Contains all state and resources needed to manage an MCP server instance,
 * including client registry, transport operations, request processing workers,
 * and message queues for handling MCP protocol requests and responses.
 */
struct mcp_server_ctx {
	uint8_t idx;
	struct mcp_client_registry client_registry;
	struct mcp_transport_ops *transport_mechanism;
	struct k_thread request_workers[CONFIG_MCP_REQUEST_WORKERS];
	struct k_msgq request_queue;
	char request_queue_buffers[CONFIG_MCP_REQUEST_QUEUE_SIZE * sizeof(mcp_queue_msg_t)];
	bool in_use;
	struct mcp_tool_registry tool_registry;
	struct mcp_execution_registry execution_registry;
#if defined(CONFIG_MCP_HEALTH_MONITOR)
	struct k_thread health_monitor_thread;
#endif
};

static struct mcp_server_ctx mcp_servers[CONFIG_MCP_SERVER_COUNT];

K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks, CONFIG_MCP_REQUEST_WORKERS*CONFIG_MCP_SERVER_COUNT, 2048);
#if defined(CONFIG_MCP_HEALTH_MONITOR)
K_THREAD_STACK_ARRAY_DEFINE(mcp_health_monitor_stack, CONFIG_MCP_SERVER_COUNT, 2048);
#endif

static int generate_client_id(struct mcp_server_ctx *server, uint32_t *client_id)
{
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	*client_id = 0;

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock registry mutex: %d", ret);
		return -EBUSY;
	}

	do {
		*client_id = sys_rand32_get();
		for (uint32_t i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
			if ((client_registry->clients[i].lifecycle_state != MCP_LIFECYCLE_DEINITIALIZED) &&
				(client_registry->clients[i].client_id == *client_id)) {
				*client_id = 0;
			}
		}
	} while (*client_id == 0);

	k_mutex_unlock(&client_registry->registry_mutex);

	return 0;
}

static int find_client_index(struct mcp_server_ctx *server, uint32_t client_id)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].client_id == client_id) {
			return i;
		}
	}

	return -1;
}

static int find_request_index(struct mcp_server_ctx *server, int client_index, uint32_t request_id)
{
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_client_context *client = &client_registry->clients[client_index];

	for (int i = 0; i < ARRAY_SIZE(client->active_requests); i++) {
		if (client->active_requests[i] == request_id) {
			return i;
		}
	}

	return -1;
}

static int find_tool_index(struct mcp_server_ctx *server, const char *tool_name)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	for (int i = 0; i < ARRAY_SIZE(tool_registry->tools); i++) {
		if (tool_registry->tools[i].metadata.name[0] != '\0' &&
			strncmp(tool_registry->tools[i].metadata.name, tool_name,
				CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			return i;
		}
	}

	return -1;
}

static int find_token_index(struct mcp_server_ctx *server, const uint32_t execution_token)
{
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		if (execution_registry->executions[i].execution_token == execution_token) {
			return i;
		}
	}

	return -1;
}

static int find_available_client_slot(struct mcp_server_ctx *server)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	for (int i = 0; i < ARRAY_SIZE(client_registry->clients); i++) {
		if (client_registry->clients[i].client_id == 0) {
			return i;
		}
	}

	return -1;
}

static int find_available_tool_slot(struct mcp_server_ctx *server)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	for (int i = 0; i < ARRAY_SIZE(tool_registry->tools); i++) {
		if (tool_registry->tools[i].metadata.name[0] == '\0') {
			return i;
		}
	}

	return -1;
}

static int find_available_request_slot(struct mcp_server_ctx *server, int client_index)
{
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_client_context *client = &client_registry->clients[client_index];

	for (int i = 0; i < ARRAY_SIZE(client->active_requests); i++) {
		if (client->active_requests[i] == 0) {
			return i;
		}
	}

	return -1;
}

/* Must be called with registry_mutex held */
static void cleanup_client_registry_entry(struct mcp_server_ctx *server, int client_index)
{
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_client_context *client = &client_registry->clients[client_index];

	client->client_id = 0;
	client->active_request_count = 0;

	for (int i = 0; i < ARRAY_SIZE(client->active_requests); i++) {
		client->active_requests[i] = 0;
	}

	client->lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;
	client_registry->client_count--;
}

static int register_new_client(struct mcp_server_ctx *server, uint32_t client_id)
{
	int slot_index;
	struct mcp_client_registry *client_registry = &server->client_registry;

	if (client_registry->client_count >= ARRAY_SIZE(client_registry->clients)) {
		return -ENOSPC;
	}

	slot_index = find_available_client_slot(server);
	if (slot_index == -1) {
		return -ENOSPC;
	}

	client_registry->clients[slot_index].client_id = client_id;
	client_registry->clients[slot_index].lifecycle_state = MCP_LIFECYCLE_NEW;
	client_registry->clients[slot_index].active_request_count = 0;
	client_registry->client_count++;

	return slot_index;
}

static int send_error_response(struct mcp_server_ctx *server, uint32_t request_id, uint32_t client_id, mcp_queue_msg_type_t error_type,
				   int32_t error_code, const char *error_message)
{
	mcp_error_response_t *error_response;
	int ret;

	error_response = (mcp_error_response_t *)mcp_alloc(sizeof(mcp_error_response_t));
	if (!error_response) {
		LOG_ERR("Failed to allocate error response");
		return -ENOMEM;
	}

	error_response->request_id = request_id;
	error_response->error_code = error_code;
	strncpy(error_response->error_message, error_message,
		sizeof(error_response->error_message) - 1);
	error_response->error_message[sizeof(error_response->error_message) - 1] = '\0';

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (!json_buffer) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(error_response);
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_error_response(error_response, (char *)json_buffer,
					CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(error_response);
		mcp_free(json_buffer);
		return ret;
	}

	ret = server->transport_mechanism->send(client_id, json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send error response");
		mcp_free(error_response);
		mcp_free(json_buffer);
		return -EIO;
	}

	mcp_free(error_response);
	return 0;
}

#ifdef CONFIG_MCP_HEALTH_MONITOR
/*
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
			mcp_execution_context_t *context = &execution_registry->executions[i];

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
							(uint32_t) context->worker_id);
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
						(uint32_t) context->worker_id);
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
							(uint32_t) context->worker_id);
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

static uint32_t generate_execution_token(uint32_t request_id)
{
	/* Mocking the generation for the 1st phase of development. Security phase
	 * will replace this with UUID generation to make token guessing harder.
	 */
	return request_id;
}

static int create_execution_context(struct mcp_server_ctx *server, mcp_tools_call_request_t *request, uint32_t *execution_token,
					int *execution_token_index)
{
	int ret;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	if (request == NULL || execution_token == NULL || execution_token_index == NULL) {
		LOG_ERR("Invalid parameter(s)");
		return -EINVAL;
	}

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	*execution_token = generate_execution_token(request->request_id);

	bool found_slot = false;

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		if (execution_registry->executions[i].execution_token == 0) {
			mcp_execution_context_t *context = &execution_registry->executions[i];

			context->execution_token = *execution_token;
			context->request_id = request->request_id;
			context->client_id = request->client_id;
			context->worker_id = k_current_get();
			context->start_timestamp = k_uptime_get();
			context->cancel_timestamp = 0;
			context->last_message_timestamp = k_uptime_get();
			context->worker_released = false;
			context->execution_state = MCP_EXEC_ACTIVE;

			found_slot = true;
			*execution_token_index = i;
			break;
		}
	}

	k_mutex_unlock(&execution_registry->registry_mutex);

	if (!found_slot) {
		LOG_ERR("Execution registry full");
		return -ENOENT;
	}

	return 0;
}

static int set_worker_released_execution_context(struct mcp_server_ctx *server, int execution_token_index)
{
	int ret;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);

		return ret;
	}
	LOG_DBG("Releasing worker for execution token index: %d", execution_token_index);
	execution_registry->executions[execution_token_index].worker_released = true;
	LOG_DBG("Released worker for execution token index: %d", execution_token_index);
	k_mutex_unlock(&execution_registry->registry_mutex);
	return 0;
}

static int destroy_execution_context(struct mcp_server_ctx *server, uint32_t execution_token)
{
	int ret;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	bool found_token = false;

	for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
		if (execution_registry->executions[i].execution_token == execution_token) {
			mcp_execution_context_t *context = &execution_registry->executions[i];

			memset(context, 0, sizeof(*context));
			found_token = true;
			break;
		}
	}

	k_mutex_unlock(&execution_registry->registry_mutex);

	if (!found_token) {
		LOG_ERR("Unknown execution token");
		return -ENOENT;
	}

	return 0;
}

/* Message handlers */

static int handle_system_message(struct mcp_server_ctx *server, mcp_system_msg_t *system_msg)
{
	int ret;
	int client_index;
	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_execution_registry *execution_registry = &server->execution_registry;

	LOG_DBG("Processing system request");

	switch (system_msg->type) {
	case MCP_SYS_CLIENT_SHUTDOWN:
		ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry mutex: %d", ret);
			return ret;
		}

		client_index = find_client_index(server, system_msg->client_id);
		if (client_index == -1) {
			LOG_ERR("Client not registered");
			k_mutex_unlock(&client_registry->registry_mutex);
			return -ENOENT;
		}

		client_registry->clients[client_index].lifecycle_state =
			MCP_LIFECYCLE_DEINITIALIZING;

		ret = k_mutex_lock(&execution_registry->registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock execution registry: %d", ret);
		} else {
			for (int i = 0; i < ARRAY_SIZE(execution_registry->executions); i++) {
				if (execution_registry->executions[i].execution_token != 0 &&
					execution_registry->executions[i].client_id ==
						system_msg->client_id) {
					uint32_t *cancel_token =
						(uint32_t *)mcp_alloc(sizeof(uint32_t));
					if (cancel_token == NULL) {
						ret = -ENOMEM;
						LOG_ERR("Failed to allocate cancel_token, cannot "
							"cancel tool execution %d for client %d.",
							i, system_msg->client_id);
						continue;
					}

					LOG_DBG("Requesting cancellation for execution token %u",
						execution_registry->executions[i].execution_token);
					execution_registry->executions[i].execution_state =
						MCP_EXEC_CANCELED;
					execution_registry->executions[i].cancel_timestamp =
						k_uptime_get();
				}
			}
			k_mutex_unlock(&execution_registry->registry_mutex);
		}

		cleanup_client_registry_entry(server, client_index);
		k_mutex_unlock(&client_registry->registry_mutex);
		break;

	default:
		LOG_ERR("Unknown system message type: %u", system_msg->type);
		return -EINVAL;
	}

	return 0;
}

static int handle_initialize_request(struct mcp_server_ctx *server, mcp_initialize_request_t *request)
{
	mcp_initialize_response_t *response_data;
	int client_index;
	int ret;
	struct mcp_client_registry *client_registry = &server->client_registry;

	LOG_DBG("Processing initialize request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	client_index = find_client_index(server, request->client_id);
	if (client_index == -1) {
		client_index = register_new_client(server, request->client_id);
		if (client_index < 0) {
			LOG_ERR("Client registry full");
			k_mutex_unlock(&client_registry->registry_mutex);
			return client_index;
		}
	}

	/* State transition: NEW -> INITIALIZING */
	if (client_registry->clients[client_index].lifecycle_state == MCP_LIFECYCLE_NEW) {
		client_registry->clients[client_index].lifecycle_state = MCP_LIFECYCLE_INITIALIZING;
	} else {
		LOG_ERR("Client %u invalid state for initialization", request->client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EALREADY;
	}

	k_mutex_unlock(&client_registry->registry_mutex);

	response_data = (mcp_initialize_response_t *)mcp_alloc(sizeof(mcp_initialize_response_t));
	if (!response_data) {
		LOG_ERR("Failed to allocate response");
		return -ENOMEM;
	}

	response_data->request_id = request->request_id;
	response_data->capabilities = 0;
	response_data->capabilities |= MCP_TOOLS;

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (!json_buffer) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(response_data);
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_initialize_response(response_data, (char *)json_buffer,
					CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		return ret;
	}

	ret = server->transport_mechanism->send(request->client_id, json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send initialize response %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		return -EIO;
	}

	mcp_free(response_data);
	return 0;
}

static int validate_client_for_tools_request(struct mcp_server_ctx *server, uint32_t client_id, int *client_index)
{
	struct mcp_client_registry *client_registry = &server->client_registry;

	*client_index = find_client_index(server, client_id);
	if (*client_index == -1) {
		LOG_DBG("Client not found: %u", client_id);
		return -ENOENT;
	}

	if (client_registry->clients[*client_index].lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		LOG_DBG("Client not in initialized state: %u", client_id);
		return -EPERM;
	}

	return 0;
}

static int copy_tool_metadata_to_response(struct mcp_server_ctx *server, mcp_tools_list_response_t *response_data)
{
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	response_data->tool_count = tool_registry->tool_count;

	for (int i = 0; i < tool_registry->tool_count; i++) {
		strncpy(response_data->tools[i].name, tool_registry->tools[i].metadata.name,
			CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
		response_data->tools[i].name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

		strncpy(response_data->tools[i].input_schema,
			tool_registry->tools[i].metadata.input_schema,
			CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1);
		response_data->tools[i].input_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1] = '\0';

#ifdef CONFIG_MCP_TOOL_DESC
		if (strlen(tool_registry->tools[i].metadata.description) > 0) {
			strncpy(response_data->tools[i].description,
				tool_registry->tools[i].metadata.description,
				CONFIG_MCP_TOOL_DESC_MAX_LEN - 1);
			response_data->tools[i].description[CONFIG_MCP_TOOL_DESC_MAX_LEN - 1] =
				'\0';
		} else {
			response_data->tools[i].description[0] = '\0';
		}
#endif

#ifdef CONFIG_MCP_TOOL_TITLE
		if (strlen(tool_registry->tools[i].metadata.title) > 0) {
			strncpy(response_data->tools[i].title,
				tool_registry->tools[i].metadata.title,
				CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
			response_data->tools[i].title[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';
		} else {
			response_data->tools[i].title[0] = '\0';
		}
#endif

#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
		if (strlen(tool_registry->tools[i].metadata.output_schema) > 0) {
			strncpy(response_data->tools[i].output_schema,
				tool_registry->tools[i].metadata.output_schema,
				CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1);
			response_data->tools[i].output_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1] =
				'\0';
		} else {
			response_data->tools[i].output_schema[0] = '\0';
		}
#endif
	}

	return 0;
}

static int handle_tools_list_request(struct mcp_server_ctx *server, mcp_tools_list_request_t *request)
{
	mcp_tools_list_response_t *response_data;
	int client_index;
	int ret;

	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	LOG_DBG("Processing tools list request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	ret = validate_client_for_tools_request(server, request->client_id, &client_index);
	k_mutex_unlock(&client_registry->registry_mutex);

	if (ret != 0) {
		return ret;
	}

	response_data = (mcp_tools_list_response_t *)mcp_alloc(sizeof(mcp_tools_list_response_t));
	if (!response_data) {
		LOG_ERR("Failed to allocate response");
		return -ENOMEM;
	}

	response_data->request_id = request->request_id;

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry");
		mcp_free(response_data);
		return ret;
	}

	copy_tool_metadata_to_response(server, response_data);
	k_mutex_unlock(&tool_registry->registry_mutex);

	/* Allocate buffer for serialization */
	uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (!json_buffer) {
		LOG_ERR("Failed to allocate buffer, dropping message");
		mcp_free(response_data);
		return -ENOMEM;
	}

	/* Serialize response to JSON */
	ret = mcp_json_serialize_tools_list_response(response_data, (char *)json_buffer,
					CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
	if (ret <= 0) {
		LOG_ERR("Failed to serialize response: %d", ret);
		mcp_free(response_data);
		mcp_free(json_buffer);
		return ret;
	}

	ret = server->transport_mechanism->send(request->client_id, json_buffer, ret);
	if (ret) {
		LOG_ERR("Failed to send tools list response");
		mcp_free(response_data);
		mcp_free(json_buffer);
		return -EIO;
	}

	mcp_free(response_data);
	return 0;
}

static int handle_tools_call_request(struct mcp_server_ctx *server, mcp_tools_call_request_t *request)
{
	int ret;
	int tool_index;
	int client_index;
	int execution_token_index;
	int next_active_request_index;
	mcp_tool_callback_t callback;
	uint32_t execution_token;

	struct mcp_client_registry *client_registry = &server->client_registry;
	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	LOG_DBG("Processing tools call request");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	ret = validate_client_for_tools_request(server, request->client_id, &client_index);
	if (ret != 0) {
		k_mutex_unlock(&client_registry->registry_mutex);
		return ret;
	}

	next_active_request_index = find_available_request_slot(server, client_index);
	if (next_active_request_index == -1) {
		LOG_ERR("No available request slot for client");
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOSPC;
	}

	client_registry->clients[client_index].active_requests[next_active_request_index] =
		request->request_id;
	client_registry->clients[client_index].active_request_count++;

	k_mutex_unlock(&client_registry->registry_mutex);

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		goto cleanup_active_request;
	}

	tool_index = find_tool_index(server, request->name);
	if (tool_index == -1) {
		LOG_ERR("Tool '%s' not found", request->name);
		k_mutex_unlock(&tool_registry->registry_mutex);
		ret = -ENOENT;
		goto cleanup_active_request;
	}

	callback = tool_registry->tools[tool_index].callback;
	k_mutex_unlock(&tool_registry->registry_mutex);

	ret = create_execution_context(server, request, &execution_token, &execution_token_index);
	if (ret != 0) {
		LOG_ERR("Failed to create execution context: %d", ret);
		goto cleanup_active_request;
	}

	ret = callback(request->arguments, execution_token);
	if (ret != 0) {
		LOG_ERR("Tool callback failed: %d", ret);
		destroy_execution_context(server, execution_token);
		goto cleanup_active_request;
	}

	ret = set_worker_released_execution_context(server, execution_token_index);
	if (ret != 0) {
		LOG_ERR("Setting worker released to true failed: %d", ret);
		/* TODO: Cancel tool execution */
		destroy_execution_context(server, execution_token);
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

	client_registry->clients[client_index].active_requests[next_active_request_index] = 0;
	client_registry->clients[client_index].active_request_count--;

	k_mutex_unlock(&client_registry->registry_mutex);
	return final_ret;
}

static int handle_notification(struct mcp_server_ctx *server, mcp_client_notification_t *notification)
{
	int ret;
	int client_index;
	struct mcp_client_registry *client_registry = &server->client_registry;

	LOG_DBG("Processing notification");

	ret = k_mutex_lock(&client_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry");
		return ret;
	}

	client_index = find_client_index(server, notification->client_id);
	if (client_index == -1) {
		LOG_ERR("Client not found %x", notification->client_id);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -ENOENT;
	}

	switch (notification->method) {
	case MCP_NOTIF_INITIALIZED:
		/* State transition: INITIALIZING -> INITIALIZED */
		if (client_registry->clients[client_index].lifecycle_state ==
			MCP_LIFECYCLE_INITIALIZING) {
			client_registry->clients[client_index].lifecycle_state =
				MCP_LIFECYCLE_INITIALIZED;
		} else {
			LOG_ERR("Invalid state transition for client %u", notification->client_id);
			k_mutex_unlock(&client_registry->registry_mutex);
			return -EPERM;
		}
		break;

	default:
		LOG_ERR("Unknown notification method %u", notification->method);
		k_mutex_unlock(&client_registry->registry_mutex);
		return -EINVAL;
	}

	k_mutex_unlock(&client_registry->registry_mutex);
	return 0;
}

/* @brief Worker threads
 * @param ctx pointer to server context
 * @param wid worker id
 * @param arg3 NULL
 */
static void mcp_request_worker(void *ctx, void *wid, void *arg3)
{
	mcp_queue_msg_t request;
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

		switch (request.type) {
		case MCP_MSG_SYSTEM: {
			mcp_system_msg_t *system_msg = (mcp_system_msg_t *)request.data;
			system_msg->client_id = request.client_id;

			ret = handle_system_message(server, system_msg);
			if (ret != 0) {
				LOG_ERR("System message failed: %d", ret);
			}
			mcp_free(system_msg);
			break;
		}

		case MCP_MSG_REQUEST_INITIALIZE: {
			mcp_initialize_request_t *init_request =
				(mcp_initialize_request_t *)request.data;
			init_request->client_id = request.client_id;

			ret = handle_initialize_request(server, init_request);
			if (ret != 0) {
				LOG_ERR("Initialize request failed: %d", ret);
				if (ret == -EALREADY) {
					send_error_response(server,
						init_request->request_id, request.client_id, MCP_MSG_ERROR_INITIALIZE,
						MCP_ERROR_INVALID_PARAMS,
						"Client already initialized or in invalid state");
				} else {
					send_error_response(server, init_request->request_id, request.client_id,
								MCP_MSG_ERROR_INITIALIZE,
								MCP_ERROR_INTERNAL_ERROR,
								"Server initialization failed");
				}
			}
			mcp_free(init_request);
			break;
		}

		case MCP_MSG_REQUEST_TOOLS_LIST: {
			mcp_tools_list_request_t *tools_list_request =
				(mcp_tools_list_request_t *)request.data;
			tools_list_request->client_id = request.client_id;

			ret = handle_tools_list_request(server, tools_list_request);
			if (ret != 0) {
				LOG_ERR("Tools list request failed: %d", ret);

				int32_t error_code;
				const char *error_msg;

				if (ret == -ENOENT || ret == -EPERM) {
					error_code = MCP_ERROR_INVALID_PARAMS;
					error_msg = "Client not found or not initialized";
				} else {
					error_code = MCP_ERROR_INTERNAL_ERROR;
					error_msg = "Internal server error processing tools list";
				}

				send_error_response(server, tools_list_request->request_id, request.client_id,
							MCP_MSG_ERROR_TOOLS_LIST, error_code,
							error_msg);
			}
			mcp_free(tools_list_request);
			break;
		}

		case MCP_MSG_REQUEST_TOOLS_CALL: {
			mcp_tools_call_request_t *tools_call_request =
				(mcp_tools_call_request_t *)request.data;
			tools_call_request->client_id = request.client_id;

			ret = handle_tools_call_request(server, tools_call_request);
			if (ret != 0) {
				LOG_ERR("Tools call request failed: %d", ret);

				int32_t error_code;
				const char *error_msg;

				if (ret == -ENOENT) {
					error_code = MCP_ERROR_METHOD_NOT_FOUND;
					error_msg = "Tool not found";
				} else if (ret == -EPERM) {
					error_code = MCP_ERROR_INVALID_PARAMS;
					error_msg = "Client not authorized for tool execution";
				} else if (ret == -ENOSPC) {
					error_code = MCP_ERROR_SERVER_ERROR;
					error_msg = "No available execution slots";
				} else {
					error_code = MCP_ERROR_INTERNAL_ERROR;
					error_msg = "Internal server error processing tool call";
				}

				send_error_response(server, tools_call_request->request_id, request.client_id,
							MCP_MSG_ERROR_TOOLS_CALL, error_code,
							error_msg);
			}
			mcp_free(tools_call_request);
			break;
		}
		case MCP_MSG_NOTIFICATION: {
			mcp_client_notification_t *notification =
				(mcp_client_notification_t *)request.data;
			notification->client_id = request.client_id;

			ret = handle_notification(server, notification);
			if (ret != 0) {
				LOG_ERR("Notification failed: %d", ret);
			}
			mcp_free(notification);
			break;
		}

		default:
			LOG_ERR("Unknown message type %u", request.type);
			mcp_free(request.data);
			break;
		}
	}
}

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
 * Internal Interface Implementation
 ******************************************************************************/
int mcp_server_handle_request(mcp_server_ctx_t ctx, const char *json, size_t length, uint32_t in_client_id, uint32_t *out_client_id, mcp_queue_msg_type_t *msg_type)
{
	mcp_queue_msg_t msg;
	int ret;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (!server || !out_client_id) {
		LOG_ERR("Invalid server or output client id pointer");
		return -EINVAL;
	}

	*msg_type = MCP_MSG_UNKNOWN;

	if (!json || length == 0) {
		LOG_ERR("Invalid request parameters");
		return -EINVAL;
	}

	LOG_DBG("Transport parsing JSON request from client %x (%zu bytes)", in_client_id, length);

	/* Parse JSON request */
	ret = mcp_json_parse_request(json, length, &msg.type, &msg.data);
	if (ret) {
		LOG_ERR("Failed to parse JSON request: %d", ret);
		return -EINVAL;
	}

	if (!msg.data) {
		LOG_ERR("JSON parsing returned NULL data");
		return -EINVAL;
	}

	/* Determine the client id. Initialize request requires a new client id. */
	switch (msg.type) {
	case MCP_MSG_REQUEST_INITIALIZE:
		/* Assign a new client id */
		ret = generate_client_id(server, out_client_id);
		if (ret) {
			LOG_ERR("Unable to geenrate session id");
			return ret;
		}

		msg.client_id = *out_client_id;
		break;
	case MCP_MSG_REQUEST_TOOLS_LIST:
	case MCP_MSG_REQUEST_TOOLS_CALL:
	case MCP_MSG_SYSTEM:
	case MCP_MSG_NOTIFICATION:
		ret = find_client_index(server, in_client_id);
		if (ret == -1)
		{
			LOG_ERR("Can't find client with id %u", in_client_id);
			return -ENOENT;
		}

		msg.client_id = in_client_id;
		*out_client_id = in_client_id;
		break;
	default:
		LOG_WRN("Request not recognized. Dropping.");
		return -EINVAL;
		break;
	}

	ret = k_msgq_put(&server->request_queue, &msg, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("Failed to submit request: %d", ret);
		return -ENOMEM;
	}

	*msg_type = msg.type;
	return 0;
}

/*******************************************************************************
 * Interface Implementation
 ******************************************************************************/
mcp_server_ctx_t mcp_server_init(struct mcp_transport_ops *transport_ops)
{
	int ret;

	LOG_INF("Initializing MCP Server");

	if (transport_ops == NULL) {
		LOG_ERR("Invalid transport operations");
		return NULL;
	}

	struct mcp_server_ctx *server_ctx = allocate_mcp_server_context();
	if (server_ctx == NULL) {
		LOG_ERR("No available server contexts");
		return NULL;
	}

	server_ctx->transport_mechanism = transport_ops;
	ret = server_ctx->transport_mechanism->init(server_ctx);
	if (ret < 0) {
		LOG_ERR("Failed to initialize HTTP transport: %d", ret);
		return NULL;
	}

	k_msgq_init(&server_ctx->request_queue, server_ctx->request_queue_buffers, sizeof(mcp_queue_msg_t), CONFIG_MCP_REQUEST_QUEUE_SIZE);

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
	return (mcp_server_ctx_t) server_ctx;
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
		tid = k_thread_create(&server->request_workers[i], mcp_request_worker_stacks[thread_stack_idx],
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

#if defined(CONFIG_MCP_HEALTH_MONITOR)
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

	LOG_INF("MCP Server started: %d request, %d response workers",
		CONFIG_MCP_REQUEST_WORKERS, CONFIG_MCP_RESPONSE_WORKERS);
	return 0;
}

int mcp_server_submit_tool_message(mcp_server_ctx_t ctx, const mcp_app_message_t *app_msg, uint32_t execution_token)
{
	int ret;
	int execution_token_index;
	uint32_t request_id;
	uint32_t client_id;
	mcp_queue_msg_t response;
	mcp_tools_call_response_t *response_data;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (!server) {
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

	execution_token_index = find_token_index(server, execution_token);
	if (execution_token_index == -1) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->registry_mutex);
		return -ENOENT;
	}

	mcp_execution_context_t *execution_context =
		&execution_registry->executions[execution_token_index];
	request_id = execution_context->request_id;
	client_id = execution_context->client_id;

	bool is_execution_canceled = (execution_context->execution_state == MCP_EXEC_CANCELED);

	if (is_execution_canceled) {
		if (app_msg->type == MCP_USR_TOOL_CANCEL_ACK) {
			execution_context->execution_state = MCP_EXEC_FINISHED;
		} else if (app_msg->type == MCP_USR_TOOL_RESPONSE) {
			execution_context->execution_state = MCP_EXEC_FINISHED;
			LOG_WRN("Execution canceled, tool message will be dropped.");
		}
		k_mutex_unlock(&execution_registry->registry_mutex);
	} else {
		execution_context->last_message_timestamp = k_uptime_get();
		k_mutex_unlock(&execution_registry->registry_mutex);

		switch (app_msg->type) {
			/* Result is passed as a complete JSON string that gets attached to the full
			 * JSON RPC response inside the Transport layer
			 */
		case MCP_USR_TOOL_PING: {
			return 0;
		}

		case MCP_USR_TOOL_RESPONSE: {
			response_data =
				(mcp_tools_call_response_t *)mcp_alloc(
					sizeof(mcp_tools_call_response_t));
			if (response_data == NULL) {
				LOG_ERR("Failed to allocate memory for response");
				return -ENOMEM;
			}

			response_data->request_id = request_id;
			response_data->length = app_msg->length;

			if (app_msg->length > CONFIG_MCP_TOOL_RESULT_MAX_LEN) {
				/* TODO: Truncate or fail? */
				LOG_WRN("Result truncated to max length");
				response_data->length = CONFIG_MCP_TOOL_RESULT_MAX_LEN;
				response_data->result[CONFIG_MCP_TOOL_RESULT_MAX_LEN - 1] = '\0';
			}

			strncpy((char *)response_data->result, (char *)app_msg->data,
				response_data->length);

			response.type = MCP_MSG_RESPONSE_TOOLS_CALL;
			response.data = response_data;

			execution_context->execution_state = MCP_EXEC_FINISHED;
			break;
		}

		default:
			LOG_ERR("Unsupported application message type: %u", app_msg->type);
			return -EINVAL;
		}

		/* Allocate buffer for serialization */
		uint8_t *json_buffer = (uint8_t *)mcp_alloc(CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
		if (!json_buffer) {
			LOG_ERR("Failed to allocate buffer, dropping message");
			mcp_free(response_data);
			return -ENOMEM;
		}

		/* Serialize response to JSON */
		ret = mcp_json_serialize_tools_call_response(response_data, (char *)json_buffer,
						CONFIG_MCP_TRANSPORT_BUFFER_SIZE);
		if (ret <= 0) {
			LOG_ERR("Failed to serialize response: %d", ret);
			mcp_free(response_data);
			mcp_free(json_buffer);
			return ret;
		}

		ret = server->transport_mechanism->send(client_id, json_buffer, ret);
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

		int client_index = find_client_index(server, client_id);

		if (client_index == -1) {
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

		int request_index = find_request_index(server, client_index, request_id);

		if (ret == -1) {
			LOG_ERR("Failed to find request index in client's active requests. Client "
				"registry is broken.");
			k_mutex_unlock(&client_registry->registry_mutex);
			goto skip_client_cleanup;
		}

		client_registry->clients[client_index].active_requests[request_index] = 0;
		client_registry->clients[client_index].active_request_count--;

		k_mutex_unlock(&client_registry->registry_mutex);

skip_client_cleanup:
		ret = destroy_execution_context(server, execution_token);
		if (ret != 0) {
			LOG_ERR("Failed to destroy execution context: %d. Execution registry is "
				"broken. Message was submitted successfully.",
				ret);
			return ret;
		}
	}

	return 0;
}

int mcp_server_add_tool(mcp_server_ctx_t ctx, const mcp_tool_record_t *tool_record)
{
	int ret;
	int available_slot;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (!server) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if (!tool_record || !tool_record->metadata.name[0] || !tool_record->callback) {
		LOG_ERR("Invalid tool record");
		return -EINVAL;
	}

	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	if (find_tool_index(server, tool_record->metadata.name) != -1) {
		LOG_ERR("Tool '%s' already exists", tool_record->metadata.name);
		k_mutex_unlock(&tool_registry->registry_mutex);
		return -EEXIST;
	}

	available_slot = find_available_tool_slot(server);
	if (available_slot == -1) {
		LOG_ERR("Tool registry full");
		k_mutex_unlock(&tool_registry->registry_mutex);
		return -ENOSPC;
	}

	memcpy(&tool_registry->tools[available_slot], tool_record, sizeof(mcp_tool_record_t));
	tool_registry->tool_count++;

	LOG_INF("Tool '%s' registered at slot %d", tool_record->metadata.name, available_slot);

	k_mutex_unlock(&tool_registry->registry_mutex);
	return 0;
}

int mcp_server_remove_tool(mcp_server_ctx_t ctx, const char *tool_name)
{
	int ret;
	int tool_index;
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (!server) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if (!tool_name || !tool_name[0]) {
		LOG_ERR("Invalid tool name");
		return -EINVAL;
	}

	struct mcp_tool_registry *tool_registry = &server->tool_registry;

	ret = k_mutex_lock(&tool_registry->registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	tool_index = find_tool_index(server, tool_name);
	if (tool_index == -1) {
		k_mutex_unlock(&tool_registry->registry_mutex);
		LOG_ERR("Tool '%s' not found", tool_name);
		return -ENOENT;
	}

	memset(&tool_registry->tools[tool_index], 0, sizeof(mcp_tool_record_t));
	tool_registry->tool_count--;
	LOG_INF("Tool '%s' removed", tool_name);

	k_mutex_unlock(&tool_registry->registry_mutex);
	return 0;
}

int mcp_server_is_execution_canceled(mcp_server_ctx_t ctx, uint32_t execution_token, bool *is_canceled)
{
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;

	if (!server) {
		LOG_ERR("Invalid server context");
		return -EINVAL;
	}

	if (!is_canceled) {
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

	int index = find_token_index(server, execution_token);

	if (index == -1) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry->registry_mutex);
		*is_canceled = false;
		return -ENOENT;
	}

	*is_canceled = (execution_registry->executions[index].execution_state == MCP_EXEC_CANCELED);

	k_mutex_unlock(&execution_registry->registry_mutex);

	return 0;
}

#ifdef CONFIG_ZTEST
uint8_t mcp_server_get_client_count(mcp_server_ctx_t ctx)
{
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	return server->client_registry.client_count;
}

uint8_t mcp_server_get_tool_count(mcp_server_ctx_t ctx)
{
	struct mcp_server_ctx *server = (struct mcp_server_ctx *)ctx;
	return server->tool_registry.tool_count;
}
#endif
