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

/* Client lifecycle states */
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
	uint32_t active_requests[MCP_MAX_REQUESTS];
	uint8_t active_request_count;
} mcp_client_context_t;

typedef struct {
	mcp_client_context_t clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex registry_mutex;
	uint8_t client_count;
} mcp_client_registry_t;

static mcp_client_registry_t client_registry;
static mcp_tool_registry_t tool_registry;

K_MSGQ_DEFINE(mcp_request_queue, sizeof(mcp_request_queue_msg_t), MCP_REQUEST_QUEUE_SIZE, 4);
K_MSGQ_DEFINE(mcp_message_queue, sizeof(mcp_response_queue_msg_t), MCP_MESSAGE_QUEUE_SIZE, 4);

K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks, MCP_REQUEST_WORKERS, 2048);
K_THREAD_STACK_ARRAY_DEFINE(mcp_message_worker_stacks, MCP_MESSAGE_WORKERS, 2048);

static struct k_thread mcp_request_workers[MCP_REQUEST_WORKERS];
static struct k_thread mcp_message_workers[MCP_MESSAGE_WORKERS];

/* Must be called with registry_mutex held */
static void cleanup_client_registry_entry(int client_index)
{
	client_registry.clients[client_index].client_id = 0;
	client_registry.clients[client_index].active_request_count = 0;

	for (int i = 0; i < MCP_MAX_REQUESTS; i++) {
		client_registry.clients[client_index].active_requests[i] = 0;
	}

	client_registry.clients[client_index].lifecycle_state = MCP_LIFECYCLE_DEINITIALIZED;
	client_registry.client_count--;
}

static int handle_system_message(mcp_system_msg_t *system_msg)
{
	int ret;
	int client_index = -1;

	LOG_DBG("Processing system request");

	switch (system_msg->type) {
	case MCP_SYS_CLIENT_SHUTDOWN:
		ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock client registry mutex: %d", ret);
			return ret;
		}

		for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
			if (client_registry.clients[i].client_id == system_msg->client_id) {
				client_index = i;
				client_registry.clients[i].lifecycle_state =
					MCP_LIFECYCLE_DEINITIALIZING;
				break;
			}
		}

		if (client_index == -1) {
			LOG_ERR("Client not registered");
			k_mutex_unlock(&client_registry.registry_mutex);
			return -ENOENT;
		}

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
	mcp_response_queue_msg_t response;
	int client_index = -1;
	int ret;

	LOG_DBG("Processing initialize request");

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	/* Search for existing client */
	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (client_registry.clients[i].client_id == request->client_id) {
			client_index = i;
			break;
		}
	}

	/* Register new client if needed */
	if (client_index == -1) {
		if (client_registry.client_count >= CONFIG_HTTP_SERVER_MAX_CLIENTS) {
			LOG_ERR("Client registry full");
			k_mutex_unlock(&client_registry.registry_mutex);
			return -ENOSPC;
		}

		for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
			if (client_registry.clients[i].client_id == 0) {
				client_registry.clients[i].client_id = request->client_id;
				client_registry.clients[i].lifecycle_state = MCP_LIFECYCLE_NEW;
				client_registry.clients[i].active_request_count = 0;
				client_index = i;
				client_registry.client_count++;
				break;
			}
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

	response.type = MCP_MSG_RESPONSE_INITIALIZE;
	response.data = response_data;

	ret = mcp_transport_queue_response(&response);
	if (ret != 0) {
		LOG_ERR("Failed to queue response: %d", ret);
		mcp_free(response_data);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
static int handle_tools_list_request(mcp_tools_list_request_t *request)
{
	mcp_tools_list_response_t *response_data;
	mcp_response_queue_msg_t response;
	int client_index = -1;
	int ret;

	LOG_DBG("Processing tools list request");

	/* Check client state in single lock operation */
	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry mutex: %d", ret);
		return ret;
	}

	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (client_registry.clients[i].client_id == request->client_id) {
			client_index = i;
			break;
		}
	}

	if (client_index == -1) {
		k_mutex_unlock(&client_registry.registry_mutex);
		return -ENOENT;
	}

	if (client_registry.clients[client_index].lifecycle_state != MCP_LIFECYCLE_INITIALIZED) {
		k_mutex_unlock(&client_registry.registry_mutex);
		return -EPERM;
	}

	k_mutex_unlock(&client_registry.registry_mutex);

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

	response_data->tool_count = tool_registry.tool_count;

	/* Copy tool metadata */
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

	k_mutex_unlock(&tool_registry.registry_mutex);

	response.type = MCP_MSG_RESPONSE_TOOLS_LIST;
	response.data = response_data;

	ret = mcp_transport_queue_response(&response);
	if (ret != 0) {
		LOG_ERR("Failed to queue response: %d", ret);
		mcp_free(response_data);
		return ret;
	}

	return 0;
}

static int handle_tools_call_request(mcp_tools_call_request_t *request)
{
	LOG_DBG("Tool call request for client %u", request->client_id);
	/* TODO: Implement tool execution */
	return 0;
}
#endif

static int handle_notification(mcp_client_notification_t *notification)
{
	int ret;
	int client_index = -1;

	LOG_DBG("Processing notification");

	ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock client registry");
		return ret;
	}

	for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
		if (client_registry.clients[i].client_id == notification->client_id) {
			client_index = i;
			break;
		}
	}

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
			mcp_initialize_request_t *req = (mcp_initialize_request_t *)request.data;

			ret = handle_initialize_request(req);
			if (ret != 0) {
				LOG_ERR("Initialize request failed: %d", ret);
			}
			mcp_free(req);
			break;
		}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
		case MCP_MSG_REQUEST_TOOLS_LIST: {
			mcp_tools_list_request_t *req = (mcp_tools_list_request_t *)request.data;

			ret = handle_tools_list_request(req);
			if (ret != 0) {
				LOG_ERR("Tools list request failed: %d", ret);
			}
			mcp_free(req);
			break;
		}

		case MCP_MSG_REQUEST_TOOLS_CALL: {
			mcp_tools_call_request_t *req = (mcp_tools_call_request_t *)request.data;

			ret = handle_tools_call_request(req);
			if (ret != 0) {
				LOG_ERR("Tools call request failed: %d", ret);
			}
			mcp_free(req);
			break;
		}
#endif

		case MCP_MSG_NOTIFICATION: {
			mcp_client_notification_t *notif =
				(mcp_client_notification_t *)request.data;

			ret = handle_notification(notif);
			if (ret != 0) {
				LOG_ERR("Notification failed: %d", ret);
			}
			mcp_free(notif);
			break;
		}

		default:
			LOG_ERR("Unknown message type %u", request.type);
			mcp_free(request.data);
			break;
		}
	}
}

static void mcp_message_worker(void *arg1, void *arg2, void *arg3)
{
	struct mcp_message_msg msg;
	int worker_id = POINTER_TO_INT(arg1);
	int ret;

	LOG_INF("Message worker %d started", worker_id);

	while (1) {
		ret = k_msgq_get(&mcp_message_queue, &msg, K_FOREVER);
		if (ret == 0) {
			LOG_DBG("Processing message (worker %d)", worker_id);
			/* TODO: Implement message processing */
		} else {
			LOG_ERR("Failed to get message: %d", ret);
		}
	}
}

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

	memset(&client_registry.clients, 0, sizeof(client_registry.clients));
	tool_registry.tool_count = 0;
	memset(&tool_registry.tools, 0, sizeof(tool_registry.tools));

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
				      mcp_message_worker, INT_TO_POINTER(i), NULL, NULL,
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

int mcp_queue_response(void)
{
	/* TODO: Implement response queuing */
	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
int mcp_server_add_tool(const mcp_tool_record_t *tool_record)
{
	int ret;
	int available_slot = -1;

	if (!tool_record || !tool_record->metadata.name[0] || !tool_record->callback) {
		LOG_ERR("Invalid tool record");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	/* Find available slot and check for duplicates */
	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		if (tool_registry.tools[i].metadata.name[0] == '\0' && available_slot == -1) {
			available_slot = i;
		}

		if (tool_registry.tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry.tools[i].metadata.name, tool_record->metadata.name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			LOG_ERR("Tool '%s' already exists", tool_record->metadata.name);
			k_mutex_unlock(&tool_registry.registry_mutex);
			return -EEXIST;
		}
	}

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
	int tool_index = -1;

	if (!tool_name || !tool_name[0]) {
		LOG_ERR("Invalid tool name");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry: %d", ret);
		return ret;
	}

	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		if (tool_registry.tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry.tools[i].metadata.name, tool_name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			tool_index = i;
			break;
		}
	}

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
