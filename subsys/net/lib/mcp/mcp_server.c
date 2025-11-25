/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <errno.h>
#include "mcp_common.h"
#include "mcp_transport.h"

LOG_MODULE_REGISTER(mcp_server, CONFIG_MCP_LOG_LEVEL);

#define MCP_REQUEST_WORKERS    2
#define MCP_MESSAGE_WORKERS    2
#define MCP_REQUEST_QUEUE_SIZE 10
#define MCP_MESSAGE_QUEUE_SIZE 10
#define MCP_WORKER_PRIORITY    7

typedef enum {
	MCP_LIFECYCLE_DEINITIALIZED = 0,
	MCP_LIFECYCLE_NEW,
	MCP_LIFECYCLE_INITIALIZING,
	MCP_LIFECYCLE_INITIALIZED,
	MCP_LIFECYCLE_DEINITIALIZING
} mcp_lifecycle_state_t;

typedef struct {
	uint32_t client_id;
	mcp_lifecycle_state_t lifecycle_state;
	uint32_t active_requests[CONFIG_HTTP_SERVER_MAX_STREAMS];
	uint8_t active_request_count;
} mcp_client_context_t;

typedef struct {
	mcp_client_context_t clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex registry_mutex;
	uint8_t client_count;
} mcp_client_registry_t;

static mcp_client_registry_t client_registry;
static mcp_tool_registry_t tool_registry;
static mcp_execution_registry_t execution_registry;

K_MSGQ_DEFINE(mcp_request_queue, sizeof(mcp_request_queue_msg_t), MCP_REQUEST_QUEUE_SIZE, 4);
K_MSGQ_DEFINE(mcp_message_queue, sizeof(mcp_response_queue_msg_t), MCP_MESSAGE_QUEUE_SIZE, 4);

K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks, MCP_REQUEST_WORKERS, 2048);
K_THREAD_STACK_ARRAY_DEFINE(mcp_message_worker_stacks, MCP_MESSAGE_WORKERS, 2048);

static struct k_thread mcp_request_workers[MCP_REQUEST_WORKERS];
static struct k_thread mcp_message_workers[MCP_MESSAGE_WORKERS];

/* Helper functions */

static int find_client_index(uint32_t client_id)
{
	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (client_registry.clients[i].client_id == client_id) {
			return i;
		}
	}
	return -1;
}

static int find_request_index(int client_index, uint32_t request_id)
{
	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_STREAMS; i++) {
		if (client_registry.clients[client_index].active_requests[i] == request_id) {
			return i;
		}
	}
	return -1;
}

static int find_tool_index(const char *tool_name)
{
	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		if (tool_registry.tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry.tools[i].metadata.name, tool_name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			return i;
		}
	}
	return -1;
}

static int find_token_index(const uint32_t execution_token)
{
	for (int i = 0; i < MCP_MAX_REQUESTS; i++) {
		if (execution_registry.executions[i].execution_token == execution_token) {
			return i;
		}
	}
	return -1;
}

static int find_available_client_slot(void)
{
	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (client_registry.clients[i].client_id == 0) {
			return i;
		}
	}
	return -1;
}

static int find_available_tool_slot(void)
{
	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		if (tool_registry.tools[i].metadata.name[0] == '\0') {
			return i;
		}
	}
	return -1;
}

static int find_available_request_slot(int client_index)
{
	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_STREAMS; i++) {
		if (client_registry.clients[client_index].active_requests[i] == 0) {
			return i;
		}
	}
	return -1;
}

/* Must be called with registry_mutex held */
static void cleanup_client_registry_entry(int client_index)
{
	client_registry.clients[client_index].client_id = 0;
	client_registry.clients[client_index].active_request_count = 0;

	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_STREAMS; i++) {
		client_registry.clients[client_index].active_requests[i] = 0;
	}

	client_registry.clients[client_index].lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;
	client_registry.client_count--;
}

static int register_new_client(uint32_t client_id)
{
	int slot_index;

	if (client_registry.client_count >= CONFIG_HTTP_SERVER_MAX_CLIENTS) {
		return -ENOSPC;
	}

	slot_index = find_available_client_slot();
	if (slot_index == -1) {
		return -ENOSPC;
	}

	client_registry.clients[slot_index].client_id = client_id;
	client_registry.clients[slot_index].lifecycle_state = MCP_LIFECYCLE_NEW;
	client_registry.clients[slot_index].active_request_count = 0;
	client_registry.client_count++;

	return slot_index;
}

static int send_error_response(uint32_t request_id, mcp_queue_msg_type_t error_type,
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

	ret = mcp_transport_queue_response(error_type, error_response);
	if (ret != 0) {
		LOG_ERR("Failed to queue error response: %d", ret);
		mcp_free(error_response);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
static uint32_t generate_execution_token(uint32_t request_id)
{
	/* Mocking the generation for the 1st phase of development. Security phase
	 * will replace this with UUID generation to make token guessing harder.
	 */
	return request_id;
}

static int create_execution_context(mcp_tools_call_request_t *request, uint32_t *execution_token,
				    int *execution_token_index)
{
	int ret;

	if (request == NULL || execution_token == NULL || execution_token_index == NULL) {
		LOG_ERR("Invalid parameter(s)");
		return -EINVAL;
	}

	ret = k_mutex_lock(&execution_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	*execution_token = generate_execution_token(request->request_id);

	bool found_slot = false;

	for (int i = 0; i < MCP_MAX_REQUESTS; i++) {
		if (execution_registry.executions[i].execution_token == 0) {
			mcp_execution_context_t *context = &execution_registry.executions[i];

			context->execution_token = *execution_token;
			context->request_id = request->request_id;
			context->client_id = request->client_id;
			context->worker_id = k_current_get();
			context->start_timestamp = k_uptime_get();
			context->cancel_timestamp = 0;
			context->last_message_timestamp = 0;
			context->worker_released = false;
			context->execution_state = MCP_EXEC_ACTIVE;

			found_slot = true;
			*execution_token_index = i;
			break;
		}
	}

	k_mutex_unlock(&execution_registry.registry_mutex);

	if (!found_slot) {
		LOG_ERR("Execution registry full");
		return -ENOENT;
	}

	return 0;
}

static int set_worker_released_execution_context(int execution_token_index)
{
	int ret;

	ret = k_mutex_lock(&execution_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);

		return ret;
	}

	execution_registry.executions[execution_token_index].worker_released = true;

	k_mutex_unlock(&execution_registry.registry_mutex);
	return 0;
}

static int destroy_execution_context(uint32_t execution_token)
{
	int ret;

	ret = k_mutex_lock(&execution_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	bool found_token = false;

	for (int i = 0; i < MCP_MAX_REQUESTS; i++) {
		if (execution_registry.executions[i].execution_token == execution_token) {
			mcp_execution_context_t *context = &execution_registry.executions[i];

			memset(context, 0, sizeof(*context));
			found_token = true;
			break;
		}
	}

	k_mutex_unlock(&execution_registry.registry_mutex);

	if (!found_token) {
		LOG_ERR("Unknown execution token");
		return -ENOENT;
	}

	return 0;
}
#endif

/* Message handlers */

static int handle_system_message(mcp_system_msg_t *system_msg)
{
	int ret;
	int client_index;

	LOG_DBG("Processing system request");

	switch (system_msg->type) {
	case MCP_SYS_CLIENT_SHUTDOWN:
		ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry mutex: %d", ret);
			return ret;
		}

		client_index = find_client_index(system_msg->client_id);
		if (client_index == -1) {
			LOG_ERR("Client not registered");
			k_mutex_unlock(&client_registry.registry_mutex);
			return -ENOENT;
		}

		client_registry.clients[client_index].lifecycle_state =
			MCP_LIFECYCLE_DEINITIALIZING;

		/* TODO: Cancel active tool executions */
		cleanup_client_registry_entry(client_index);
		k_mutex_unlock(&client_registry.registry_mutex);
		break;

	default:
		LOG_ERR("Unknown system message type: %u", system_msg->type);
		return -EINVAL;
	}

	return 0;
}

static int handle_initialize_request(mcp_initialize_request_t *request)
{
	mcp_initialize_response_t *response_data;
	int client_index;
	int ret;

	LOG_DBG("Processing initialize request");
	/* TODO: Handle sending error responses to client */

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	client_index = find_client_index(request->client_id);
	if (client_index == -1) {
		client_index = register_new_client(request->client_id);
		if (client_index < 0) {
			LOG_ERR("Client registry full");
			k_mutex_unlock(&client_registry.registry_mutex);
			return client_index;
		}
	}

	/* State transition: NEW -> INITIALIZING */
	if (client_registry.clients[client_index].lifecycle_state == MCP_LIFECYCLE_NEW) {
		client_registry.clients[client_index].lifecycle_state = MCP_LIFECYCLE_INITIALIZING;
	} else {
		LOG_ERR("Client %u invalid state for initialization", request->client_id);
		k_mutex_unlock(&client_registry.registry_mutex);
		return -EALREADY;
	}

	k_mutex_unlock(&client_registry.registry_mutex);

	response_data = (mcp_initialize_response_t *)mcp_alloc(sizeof(mcp_initialize_response_t));
	if (!response_data) {
		LOG_ERR("Failed to allocate response");
		return -ENOMEM;
	}

	response_data->request_id = request->request_id;
	response_data->capabilities = 0;
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	response_data->capabilities |= MCP_TOOLS;
#endif

	ret = mcp_transport_queue_response(MCP_MSG_RESPONSE_INITIALIZE, response_data);
	if (ret != 0) {
		LOG_ERR("Failed to queue response: %d", ret);
		mcp_free(response_data);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
static int validate_client_for_tools_request(uint32_t client_id, int *client_index)
{
	*client_index = find_client_index(client_id);
	if (*client_index == -1) {
		return -ENOENT;
	}

	if (client_registry.clients[*client_index].lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		return -EPERM;
	}

	return 0;
}

static int copy_tool_metadata_to_response(mcp_tools_list_response_t *response_data)
{
	response_data->tool_count = tool_registry.tool_count;

	for (int i = 0; i < tool_registry.tool_count; i++) {
		strncpy(response_data->tools[i].name, tool_registry.tools[i].metadata.name,
			CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
		response_data->tools[i].name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

		strncpy(response_data->tools[i].input_schema,
			tool_registry.tools[i].metadata.input_schema,
			CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1);
		response_data->tools[i].input_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1] = '\0';

#ifdef CONFIG_MCP_TOOL_DESC
		if (strlen(tool_registry.tools[i].metadata.description) > 0) {
			strncpy(response_data->tools[i].description,
				tool_registry.tools[i].metadata.description,
				CONFIG_MCP_TOOL_DESC_MAX_LEN - 1);
			response_data->tools[i].description[CONFIG_MCP_TOOL_DESC_MAX_LEN - 1] =
				'\0';
		} else {
			response_data->tools[i].description[0] = '\0';
		}
#endif

#ifdef CONFIG_MCP_TOOL_TITLE
		if (strlen(tool_registry.tools[i].metadata.title) > 0) {
			strncpy(response_data->tools[i].title,
				tool_registry.tools[i].metadata.title,
				CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
			response_data->tools[i].title[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';
		} else {
			response_data->tools[i].title[0] = '\0';
		}
#endif

#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
		if (strlen(tool_registry.tools[i].metadata.output_schema) > 0) {
			strncpy(response_data->tools[i].output_schema,
				tool_registry.tools[i].metadata.output_schema,
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

static int handle_tools_list_request(mcp_tools_list_request_t *request)
{
	mcp_tools_list_response_t *response_data;
	int client_index;
	int ret;

	/* TODO: Handle sending error responses to client */
	LOG_DBG("Processing tools list request");

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	ret = validate_client_for_tools_request(request->client_id, &client_index);
	k_mutex_unlock(&client_registry.registry_mutex);

	if (ret != 0) {
		return ret;
	}

	response_data = (mcp_tools_list_response_t *)mcp_alloc(sizeof(mcp_tools_list_response_t));
	if (!response_data) {
		LOG_ERR("Failed to allocate response");
		return -ENOMEM;
	}

	response_data->request_id = request->request_id;

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry");
		mcp_free(response_data);
		return ret;
	}

	copy_tool_metadata_to_response(response_data);
	k_mutex_unlock(&tool_registry.registry_mutex);

	ret = mcp_transport_queue_response(MCP_MSG_RESPONSE_TOOLS_LIST, response_data);
	if (ret != 0) {
		LOG_ERR("Failed to queue response: %d", ret);
		mcp_free(response_data);
		return ret;
	}

	return 0;
}

static int handle_tools_call_request(mcp_tools_call_request_t *request)
{
	int ret;
	int tool_index;
	int client_index;
	int execution_token_index;
	int next_active_request_index;
	mcp_tool_callback_t callback;
	uint32_t execution_token;

	LOG_DBG("Processing tools call request");

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	ret = validate_client_for_tools_request(request->client_id, &client_index);
	if (ret != 0) {
		k_mutex_unlock(&client_registry.registry_mutex);
		return ret;
	}

	next_active_request_index = find_available_request_slot(client_index);
	if (next_active_request_index == -1) {
		LOG_ERR("No available request slot for client");
		k_mutex_unlock(&client_registry.registry_mutex);
		return -ENOSPC;
	}

	client_registry.clients[client_index].active_requests[next_active_request_index] =
		request->request_id;
	client_registry.clients[client_index].active_request_count++;

	k_mutex_unlock(&client_registry.registry_mutex);

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		goto cleanup_active_request;
	}

	tool_index = find_tool_index(request->name);
	if (tool_index == -1) {
		LOG_ERR("Tool '%s' not found", request->name);
		k_mutex_unlock(&tool_registry.registry_mutex);
		ret = -ENOENT;
		goto cleanup_active_request;
	}

	callback = tool_registry.tools[tool_index].callback;
	k_mutex_unlock(&tool_registry.registry_mutex);

	ret = create_execution_context(request, &execution_token, &execution_token_index);
	if (ret != 0) {
		LOG_ERR("Failed to create execution context: %d", ret);
		goto cleanup_active_request;
	}

	ret = callback(request->arguments, execution_token);
	if (ret != 0) {
		LOG_ERR("Tool callback failed: %d", ret);
		destroy_execution_context(execution_token);
		goto cleanup_active_request;
	}

	ret = set_worker_released_execution_context(execution_token_index);
	if (ret != 0) {
		LOG_ERR("Setting worker released to true failed: %d", ret);
		/* TODO: Cancel tool execution */
		destroy_execution_context(execution_token);
		goto cleanup_active_request;
	}

	return 0;

cleanup_active_request:
	int final_ret = ret;
	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d. Client registry is broken.",
			ret);
		return ret;
	}

	client_registry.clients[client_index].active_requests[next_active_request_index] = 0;
	client_registry.clients[client_index].active_request_count--;

	k_mutex_unlock(&client_registry.registry_mutex);
	return final_ret;
}
#endif

static int handle_notification(mcp_client_notification_t *notification)
{
	int ret;
	int client_index;

	LOG_DBG("Processing notification");

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry");
		return ret;
	}

	client_index = find_client_index(notification->client_id);
	if (client_index == -1) {
		LOG_ERR("Client not found");
		k_mutex_unlock(&client_registry.registry_mutex);
		return -ENOENT;
	}

	switch (notification->method) {
	case MCP_NOTIF_INITIALIZED:
		/* State transition: INITIALIZING -> INITIALIZED */
		if (client_registry.clients[client_index].lifecycle_state ==
		    MCP_LIFECYCLE_INITIALIZING) {
			client_registry.clients[client_index].lifecycle_state =
				MCP_LIFECYCLE_INITIALIZED;
		} else {
			LOG_ERR("Invalid state transition for client %u", notification->client_id);
			k_mutex_unlock(&client_registry.registry_mutex);
			return -EPERM;
		}
		break;

	default:
		LOG_ERR("Unknown notification method %u", notification->method);
		k_mutex_unlock(&client_registry.registry_mutex);
		return -EINVAL;
	}

	k_mutex_unlock(&client_registry.registry_mutex);
	return 0;
}

/* Worker threads */
static void mcp_request_worker(void *arg1, void *arg2, void *arg3)
{
	mcp_request_queue_msg_t request;
	int worker_id = POINTER_TO_INT(arg1);
	int ret;

	LOG_INF("Request worker %d started", worker_id);

	while (1) {
		ret = k_msgq_get(&mcp_request_queue, &request, K_FOREVER);
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

			ret = handle_system_message(system_msg);
			if (ret != 0) {
				LOG_ERR("System message failed: %d", ret);
			}
			mcp_free(system_msg);
			break;
		}

		case MCP_MSG_REQUEST_INITIALIZE: {
			mcp_initialize_request_t *init_request =
				(mcp_initialize_request_t *)request.data;

			ret = handle_initialize_request(init_request);
			if (ret != 0) {
				LOG_ERR("Initialize request failed: %d", ret);
				send_error_response(init_request->request_id, 
						   MCP_MSG_ERROR_INITIALIZE,
						   MCP_ERROR_INTERNAL_ERROR,
						   "Server initialization failed");
			}
			mcp_free(init_request);
			break;
		}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
		case MCP_MSG_REQUEST_TOOLS_LIST: {
			mcp_tools_list_request_t *tools_list_request =
				(mcp_tools_list_request_t *)request.data;

			ret = handle_tools_list_request(tools_list_request);
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

				send_error_response(tools_list_request->request_id,
						   MCP_MSG_ERROR_TOOLS_LIST,
						   error_code, error_msg);
			}
			mcp_free(tools_list_request);
			break;
		}

		case MCP_MSG_REQUEST_TOOLS_CALL: {
			mcp_tools_call_request_t *tools_call_request =
				(mcp_tools_call_request_t *)request.data;

			ret = handle_tools_call_request(tools_call_request);
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

				send_error_response(tools_call_request->request_id,
						   MCP_MSG_ERROR_TOOLS_CALL,
						   error_code, error_msg);
			}
			mcp_free(tools_call_request);
			break;
		}
#endif
		case MCP_MSG_NOTIFICATION: {
			mcp_client_notification_t *notification =
				(mcp_client_notification_t *)request.data;

			ret = handle_notification(notification);
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

static void mcp_response_worker(void *arg1, void *arg2, void *arg3)
{
	mcp_response_queue_msg_t response;
	int worker_id = POINTER_TO_INT(arg1);
	int ret;

	LOG_INF("Response worker %d started", worker_id);

	while (1) {
		ret = k_msgq_get(&mcp_message_queue, &response, K_FOREVER);
		if (ret == 0) {
			LOG_DBG("Processing response (worker %d)", worker_id);
			/* TODO: When Server-sent Events and Authorization are added, implement the
			 * processing
			 */
			/* In phase 1, the response worker queue and workers are pretty much
			 * redundant by design
			 */
			/* TODO: Consider making response workers a configurable resource that can
			 * be bypassed
			 */
			ret = mcp_transport_queue_response(response.type, response.data);
			if (ret != 0) {
				LOG_ERR("Failed to queue response to transport: %d", ret);
				mcp_free(response.data);
			}
		} else {
			LOG_ERR("Failed to get response: %d", ret);
		}
	}
}

/* Public API functions */

int mcp_server_init(void)
{
	int ret;

	LOG_INF("Initializing MCP Server");

	ret = k_mutex_init(&client_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init client mutex: %d", ret);
		return ret;
	}

	ret = k_mutex_init(&tool_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init tool mutex: %d", ret);
		return ret;
	}

	ret = k_mutex_init(&execution_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init tool mutex: %d", ret);
		return ret;
	}

	memset(&client_registry.clients, 0, sizeof(client_registry.clients));
	tool_registry.tool_count = 0;
	memset(&tool_registry.tools, 0, sizeof(tool_registry.tools));
	memset(&execution_registry.executions, 0, sizeof(execution_registry.executions));

	LOG_INF("MCP Server initialized");
	return 0;
}

int mcp_server_start(void)
{
	k_tid_t tid;
	int ret;

	LOG_INF("Starting MCP Server");

	for (int i = 0; i < MCP_REQUEST_WORKERS; i++) {
		tid = k_thread_create(&mcp_request_workers[i], mcp_request_worker_stacks[i],
				      K_THREAD_STACK_SIZEOF(mcp_request_worker_stacks[i]),
				      mcp_request_worker, INT_TO_POINTER(i), NULL, NULL,
				      K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);
		if (tid == NULL) {
			LOG_ERR("Failed to create request worker %d", i);
			return -ENOMEM;
		}

		ret = k_thread_name_set(&mcp_request_workers[i], "mcp_req_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set thread name: %d", ret);
		}
	}

	for (int i = 0; i < MCP_MESSAGE_WORKERS; i++) {
		tid = k_thread_create(&mcp_message_workers[i], mcp_message_worker_stacks[i],
				      K_THREAD_STACK_SIZEOF(mcp_message_worker_stacks[i]),
				      mcp_response_worker, INT_TO_POINTER(i), NULL, NULL,
				      K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);
		if (tid == NULL) {
			LOG_ERR("Failed to create message worker %d", i);
			return -ENOMEM;
		}

		ret = k_thread_name_set(&mcp_message_workers[i], "mcp_msg_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set thread name: %d", ret);
		}
	}

	LOG_INF("MCP Server started: %d request, %d message workers", MCP_REQUEST_WORKERS,
		MCP_MESSAGE_WORKERS);

	return 0;
}

int mcp_server_submit_app_message(const mcp_app_message_t *app_msg, uint32_t execution_token)
{
	int ret;
	int execution_token_index;
	uint32_t request_id;
	uint32_t client_id;
	mcp_response_queue_msg_t response;

	if (app_msg == NULL || app_msg->data == NULL) {
		LOG_ERR("Invalid user message");
		return -EINVAL;
	}

	if (execution_token == 0) {
		LOG_ERR("Invalid execution token");
		return -EINVAL;
	}

	ret = k_mutex_lock(&execution_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock execution registry: %d", ret);
		return ret;
	}

	execution_token_index = find_token_index(execution_token);
	if (execution_token_index == -1) {
		LOG_ERR("Execution token not found");
		k_mutex_unlock(&execution_registry.registry_mutex);
		return -ENOENT;
	}

	mcp_execution_context_t *execution_context =
		&execution_registry.executions[execution_token_index];
	request_id = execution_context->request_id;
	client_id = execution_context->client_id;

	if (app_msg->type == MCP_USR_TOOL_RESPONSE) {
		execution_context->execution_state = MCP_EXEC_FINISHED;
	}

	execution_context->last_message_timestamp = k_uptime_get();
	k_mutex_unlock(&execution_registry.registry_mutex);

	switch (app_msg->type) {
		/* Result is passed as a complete JSON string that gets attached to the full JSON
		 * RPC response inside the Transport layer
		 */
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_USR_TOOL_RESPONSE: {
		mcp_tools_call_response_t *response_data =
			(mcp_tools_call_response_t *)mcp_alloc(sizeof(mcp_tools_call_response_t));
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
		break;
	}
#endif

	default:
		LOG_ERR("Unsupported application message type: %u", app_msg->type);
		return -EINVAL;
	}

	/* TODO: Make response workers configurable? If not used, call transport API directly? */
	ret = k_msgq_put(&mcp_message_queue, &response, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("Failed to submit response to message queue: %d", ret);
		mcp_free(response.data);
		return ret;
	}

	if (app_msg->type == MCP_USR_TOOL_RESPONSE) {
		ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry mutex: %d. Client registry is "
				"broken.",
				ret);
			goto skip_client_cleanup;
		}

		int client_index = find_client_index(client_id);

		if (client_index == -1) {
			LOG_ERR("Failed to find client index in the client registry. Client "
				"registry is broken.");
			k_mutex_unlock(&client_registry.registry_mutex);
			goto skip_client_cleanup;
		}

		int request_index = find_request_index(client_index, request_id);

		if (ret == -1) {
			LOG_ERR("Failed to find request index in client's active requests. Client "
				"registry is broken.");
			k_mutex_unlock(&client_registry.registry_mutex);
			goto skip_client_cleanup;
		}

		client_registry.clients[client_index].active_requests[request_index] = 0;
		client_registry.clients[client_index].active_request_count--;

		k_mutex_unlock(&client_registry.registry_mutex);

skip_client_cleanup:
		ret = destroy_execution_context(execution_token);
		if (ret != 0) {
			LOG_ERR("Failed to destroy execution context: %d. Execution registry is "
				"broken. Message was submitted successfully.",
				ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
int mcp_server_add_tool(const mcp_tool_record_t *tool_record)
{
	int ret;
	int available_slot;

	if (!tool_record || !tool_record->metadata.name[0] || !tool_record->callback) {
		LOG_ERR("Invalid tool record");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	if (find_tool_index(tool_record->metadata.name) != -1) {
		LOG_ERR("Tool '%s' already exists", tool_record->metadata.name);
		k_mutex_unlock(&tool_registry.registry_mutex);
		return -EEXIST;
	}

	available_slot = find_available_tool_slot();
	if (available_slot == -1) {
		LOG_ERR("Tool registry full");
		k_mutex_unlock(&tool_registry.registry_mutex);
		return -ENOSPC;
	}

	memcpy(&tool_registry.tools[available_slot], tool_record, sizeof(mcp_tool_record_t));
	tool_registry.tool_count++;

	LOG_INF("Tool '%s' registered at slot %d", tool_record->metadata.name, available_slot);

	k_mutex_unlock(&tool_registry.registry_mutex);
	return 0;
}

int mcp_server_remove_tool(const char *tool_name)
{
	int ret;
	int tool_index;

	if (!tool_name || !tool_name[0]) {
		LOG_ERR("Invalid tool name");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	tool_index = find_tool_index(tool_name);
	if (tool_index == -1) {
		k_mutex_unlock(&tool_registry.registry_mutex);
		LOG_ERR("Tool '%s' not found", tool_name);
		return -ENOENT;
	}

	memset(&tool_registry.tools[tool_index], 0, sizeof(mcp_tool_record_t));
	tool_registry.tool_count--;
	LOG_INF("Tool '%s' removed", tool_name);

	k_mutex_unlock(&tool_registry.registry_mutex);
	return 0;
}
#endif

#ifdef CONFIG_ZTEST
uint8_t mcp_server_get_client_count(void)
{
	return client_registry.client_count;
}

uint8_t mcp_server_get_tool_count(void)
{
	return tool_registry.tool_count;
}
#endif
