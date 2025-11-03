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

/* Configuration defaults */

#define MCP_REQUEST_WORKERS    2
#define MCP_MESSAGE_WORKERS    2
#define MCP_REQUEST_QUEUE_SIZE 10
#define MCP_MESSAGE_QUEUE_SIZE 10
#define MCP_WORKER_PRIORITY    7

/* MCP Client Registry structures */

/* MCP Lifecycle states for client connections */
typedef enum {
	MCP_LIFECYCLE_NEW,
	MCP_LIFECYCLE_INITIALIZING,
	MCP_LIFECYCLE_INITIALIZED,
	MCP_LIFECYCLE_DEINITIALIZING,
} mcp_lifecycle_state_t;

/* Client connection context */
typedef struct {
	uint32_t client_id;
	mcp_lifecycle_state_t lifecycle_state;
	uint32_t active_requests[MCP_MAX_REQUESTS];
	uint8_t active_request_count;
} mcp_client_context_t;

/* MCP Client Registry */
typedef struct {
	mcp_client_context_t clients[CONFIG_HTTP_SERVER_MAX_CLIENTS];
	struct k_mutex registry_mutex;
} mcp_client_registry_t;

/* Global client registry instance */
static mcp_client_registry_t client_registry;
static mcp_tool_registry_t tool_registry;

/* Message queues */
K_MSGQ_DEFINE(mcp_request_queue, sizeof(mcp_request_queue_msg_t), MCP_REQUEST_QUEUE_SIZE, 4);
K_MSGQ_DEFINE(mcp_message_queue, sizeof(mcp_response_queue_msg_t), MCP_MESSAGE_QUEUE_SIZE, 4);

/* Worker thread stacks */
K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks, MCP_REQUEST_WORKERS, 2048);
K_THREAD_STACK_ARRAY_DEFINE(mcp_message_worker_stacks, MCP_MESSAGE_WORKERS, 2048);

/* Worker thread structures */
static struct k_thread mcp_request_workers[MCP_REQUEST_WORKERS];
static struct k_thread mcp_message_workers[MCP_MESSAGE_WORKERS];

/* Request worker function */
static void mcp_request_worker(void *arg1, void *arg2, void *arg3)
{
	mcp_request_queue_msg_t request;
	bool client_registered = false;
	bool client_initialized = false;
	int client_index = -1;
	int worker_id = POINTER_TO_INT(arg1);
	int ret;

	LOG_INF("Request worker %d started", worker_id);

	while (1) {
		ret = k_msgq_get(&mcp_request_queue, &request, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to get request from queue: %d", ret);
			continue;
		}

		LOG_DBG("Processing request (worker %d)", worker_id);

		if (request.data == NULL) {
			LOG_ERR("Received NULL data pointer in request");
			continue;
		}
		switch (request.type) {
		case MCP_MSG_SYSTEM: {
			mcp_system_msg_t *sys_msg;

			LOG_DBG("Processing system request");
			sys_msg = (mcp_system_msg_t *)request.data;
			/* Add system request handling logic */
			mcp_free(request.data);
		} break;

		case MCP_MSG_REQUEST_INITIALIZE: {
			mcp_initialize_request_t *rpc_request;
			mcp_initialize_response_t *response;
			mcp_response_queue_msg_t queue_response;

			LOG_DBG("Processing RPC request");
			rpc_request = (mcp_initialize_request_t *)request.data;

			/* Reset client tracking variables */
			client_registered = false;
			client_index = -1;

			/* Add RPC request handling logic */
			ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to lock client registry mutex: %d", ret);
				mcp_free(rpc_request);
				break;
			}

			/* Search for existing client */
			for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
				if (client_registry.clients[i].client_id ==
				    rpc_request->client_id) {
					client_registered = true;
					client_index = i;
					LOG_DBG("Found client with ID %u", rpc_request->client_id);
					break;
				}
			}

			/* Register new client if not found */
			if (!client_registered) {
				for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
					if (client_registry.clients[i].client_id == 0) {
						/* Found an empty slot in the client registry */
						client_registry.clients[i].client_id =
							rpc_request->client_id;
						client_registry.clients[i].lifecycle_state =
							MCP_LIFECYCLE_NEW;
						client_registry.clients[i].active_request_count = 0;

						client_index = i;
						LOG_DBG("Registered new client with ID %u",
							rpc_request->client_id);
						break;
					}
				}

				if (client_index == -1) {
					LOG_ERR("Client registry is full. Cannot register new "
						"client.");
					k_mutex_unlock(&client_registry.registry_mutex);
					mcp_free(rpc_request);
					break;
				}
			}

			if (client_registry.clients[client_index].lifecycle_state ==
			    MCP_LIFECYCLE_NEW) {
				client_registry.clients[client_index].lifecycle_state =
					MCP_LIFECYCLE_INITIALIZING;
				LOG_DBG("Client %u initializing", rpc_request->client_id);
			} else {
				LOG_ERR("Client %u already initialized", rpc_request->client_id);
				k_mutex_unlock(&client_registry.registry_mutex);
				mcp_free(rpc_request);
				break;
			}
			k_mutex_unlock(&client_registry.registry_mutex);

			response = (mcp_initialize_response_t *)mcp_alloc(
				sizeof(mcp_initialize_response_t));
			if (!response) {
				LOG_ERR("Failed to allocate response for client %u",
					rpc_request->client_id);
				/* TODO: Queue error response */
				mcp_free(rpc_request);
				break;
			}

			response->request_id = rpc_request->request_id;

			/* Set capabilities based on enabled features */
			response->capabilities = 0;
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
			response->capabilities |= MCP_TOOLS;
#endif

			queue_response.type = MCP_MSG_RESPONSE_INITIALIZE;
			queue_response.data = response;

			ret = mcp_transport_queue_response(&queue_response);
			if (ret != 0) {
				LOG_ERR("Failed to queue response: %d", ret);
				mcp_free(response);
			}

			mcp_free(rpc_request);
			break;
		}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
		case MCP_MSG_REQUEST_TOOLS_LIST: {
			mcp_tools_list_request_t *rpc_request;
			mcp_tools_list_response_t *response;
			mcp_response_queue_msg_t queue_response;

			LOG_DBG("Processing RPC request");
			rpc_request = (mcp_tools_list_request_t *)request.data;

			/* Reset client tracking variables */
			client_registered = false;
			client_initialized = false;

			/* Add RPC request handling logic */
			ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to lock client registry mutex: %d", ret);
				mcp_free(rpc_request);
				break;
			}

			/* Search for existing client */
			for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
				if (client_registry.clients[i].client_id ==
				    rpc_request->client_id) {
					client_registered = true;

					if (client_registry.clients[i].lifecycle_state ==
					    MCP_LIFECYCLE_INITIALIZED) {
						LOG_DBG("Client %u initialized, processing request",
							rpc_request->client_id);

						client_initialized = true;
					}
					break;
				}
			}

			if (!client_registered) {
				LOG_ERR("Client not registered. Refusing to process tools list "
					"request.");
				/* TODO: Handle error response */
				k_mutex_unlock(&client_registry.registry_mutex);
				mcp_free(rpc_request);
				break;
			}

			if (!client_initialized) {
				LOG_ERR("Client not initialized. Refusing to process tools list "
					"request.");
				/* TODO: Handle error response */
				k_mutex_unlock(&client_registry.registry_mutex);
				mcp_free(rpc_request);
				break;
			}

			k_mutex_unlock(&client_registry.registry_mutex);

			LOG_DBG("Retrieving tools list for client %u", rpc_request->client_id);
			response = (mcp_tools_list_response_t *)mcp_alloc(
				sizeof(mcp_tools_list_response_t));
			if (!response) {
				LOG_ERR("Failed to allocate tools list response for client %u",
					rpc_request->client_id);
				mcp_free(rpc_request);
				/* TODO: Queue error response */
				break;
			}

			response->request_id = rpc_request->request_id;
			/* Lock tool registry and copy tool information */
			ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to lock tool registry mutex: %d", ret);
				mcp_free(rpc_request);
				mcp_free(response);
				break;
			}

			response->tool_count = tool_registry.tool_count;
			/* Copy tool metadata for transport layer */
			for (int i = 0; i < tool_registry.tool_count; i++) {
				/* Required fields - always copy */
				strncpy(response->tools[i].name,
					tool_registry.tools[i].metadata.name,
					CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
				response->tools[i].name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

				strncpy(response->tools[i].input_schema,
					tool_registry.tools[i].metadata.input_schema,
					CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1);
				response->tools[i]
					.input_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1] = '\0';

#ifdef CONFIG_MCP_TOOL_DESC
				/* Description - only copy if not empty */
				if (strlen(tool_registry.tools[i].metadata.description) > 0) {
					strncpy(response->tools[i].description,
						tool_registry.tools[i].metadata.description,
						CONFIG_MCP_TOOL_DESC_MAX_LEN - 1);
					response->tools[i]
						.description[CONFIG_MCP_TOOL_DESC_MAX_LEN - 1] =
						'\0';
				} else {
					response->tools[i].description[0] = '\0';
				}
#endif

#ifdef CONFIG_MCP_TOOL_TITLE
				/* Title - only copy if not empty */
				if (strlen(tool_registry.tools[i].metadata.title) > 0) {
					strncpy(response->tools[i].title,
						tool_registry.tools[i].metadata.title,
						CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
					response->tools[i].title[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] =
						'\0';
				} else {
					response->tools[i].title[0] = '\0';
				}
#endif

#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
				/* Output schema - only copy if not empty */
				if (strlen(tool_registry.tools[i].metadata.output_schema) > 0) {
					strncpy(response->tools[i].output_schema,
						tool_registry.tools[i].metadata.output_schema,
						CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1);
					response->tools[i]
						.output_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN - 1] =
						'\0';
				} else {
					response->tools[i].output_schema[0] = '\0';
				}
#endif
			}

			k_mutex_unlock(&tool_registry.registry_mutex);

			queue_response.type = MCP_MSG_RESPONSE_TOOLS_LIST;
			queue_response.data = response;

			ret = mcp_transport_queue_response(&queue_response);
			if (ret != 0) {
				LOG_ERR("Failed to queue tools list response: %d", ret);
				mcp_free(rpc_request);
				mcp_free(response);
				break;
			}

			mcp_free(rpc_request);
			break;
		}

		case MCP_MSG_REQUEST_TOOLS_CALL: {
			mcp_tools_call_request_t *rpc_request;
			mcp_tools_call_response_t *response;
			mcp_response_queue_msg_t queue_response;

			rpc_request = (mcp_tools_call_request_t *)request.data;
			LOG_DBG("Calling tool for client %u", rpc_request->client_id);
			/* TODO: Implement tool call handling */

			mcp_free(rpc_request);
			break;
		}
#endif
		case MCP_MSG_NOTIFICATION: {
			mcp_client_notification_t *notification;

			LOG_DBG("Processing RPC notification");
			notification = (mcp_client_notification_t *)request.data;

			/* Reset client tracking variables */
			client_registered = false;
			client_index = -1;

			/* Add RPC notification handling logic */
			ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to lock client registry mutex: %d", ret);
				mcp_free(notification);
				break;
			}

			/* Search for client */
			for (int i = 0; i < CONFIG_HTTP_SERVER_MAX_CLIENTS; i++) {
				if (client_registry.clients[i].client_id ==
				    notification->client_id) {
					client_registered = true;
					client_index = i;
					LOG_DBG("Found client with ID %u", notification->client_id);
					break;
				}
			}

			if (!client_registered) {
				LOG_ERR("Client not registered. Refusing to process notification.");
				/* TODO: Handle error response */
				k_mutex_unlock(&client_registry.registry_mutex);
				mcp_free(notification);
				break;
			}

			k_mutex_unlock(&client_registry.registry_mutex);

			switch (notification->method) {
			case MCP_NOTIF_INITIALIZED: {
				ret = k_mutex_lock(&client_registry.registry_mutex, K_FOREVER);
				if (ret != 0) {
					LOG_ERR("Failed to lock client registry mutex: %d", ret);
					break;
				}

				if (client_registry.clients[client_index].lifecycle_state ==
				    MCP_LIFECYCLE_INITIALIZING) {
					client_registry.clients[client_index].lifecycle_state =
						MCP_LIFECYCLE_INITIALIZED;
					LOG_DBG("Client %u initialization complete",
						notification->client_id);
				} else {
					LOG_ERR("Invalid state transition for client %u",
						notification->client_id);
					/* TODO: Respond with an error message */
					k_mutex_unlock(&client_registry.registry_mutex);
					break;
				}
				k_mutex_unlock(&client_registry.registry_mutex);
				break;
			}

			default: {
				LOG_ERR("Unknown notification method %u", notification->method);
				/* TODO: Respond with an error message */
				break;
			}
			}

			mcp_free(notification);
			break;
		}

		default: {
			LOG_ERR("Unknown message type %u", request.type);
			if (request.data) {
				mcp_free(request.data);
			}
			break;
		}
		}
	}
}

/* Message worker function */
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
			LOG_ERR("Failed to get message from queue: %d", ret);
		}
	}
}

int mcp_server_init(void)
{
	int ret;

	LOG_INF("Initializing MCP Server Core");

	/* Initialize client registry mutex */
	ret = k_mutex_init(&client_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to initialize client registry mutex: %d", ret);
		return ret;
	}

	/* Initialize tool registry mutex */
	ret = k_mutex_init(&tool_registry.registry_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to initialize tool registry mutex: %d", ret);
		return ret;
	}

	/* Initialize client registry */
	memset(&client_registry.clients, 0, sizeof(client_registry.clients));

	/* Initialize tool registry */
	tool_registry.tool_count = 0;
	memset(&tool_registry.tools, 0, sizeof(tool_registry.tools));

	LOG_INF("MCP Server Core initialized");

	return 0;
}

int mcp_server_start(void)
{
	k_tid_t tid;
	int ret;

	LOG_INF("Starting MCP Server Core");

	/* Create and start request worker threads */
	for (int i = 0; i < MCP_REQUEST_WORKERS; i++) {
		tid = k_thread_create(&mcp_request_workers[i], mcp_request_worker_stacks[i],
				      K_THREAD_STACK_SIZEOF(mcp_request_worker_stacks[i]),
				      mcp_request_worker, INT_TO_POINTER(i), NULL, NULL,
				      K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);
		if (tid == NULL) {
			LOG_ERR("Failed to create request worker thread %d", i);
			return -ENOMEM;
		}

		ret = k_thread_name_set(&mcp_request_workers[i], "mcp_req_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set name for request worker thread %d: %d", i, ret);
		}
	}

	/* Create and start message worker threads */
	for (int i = 0; i < MCP_MESSAGE_WORKERS; i++) {
		tid = k_thread_create(&mcp_message_workers[i], mcp_message_worker_stacks[i],
				      K_THREAD_STACK_SIZEOF(mcp_message_worker_stacks[i]),
				      mcp_message_worker, INT_TO_POINTER(i), NULL, NULL,
				      K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);
		if (tid == NULL) {
			LOG_ERR("Failed to create message worker thread %d", i);
			return -ENOMEM;
		}

		ret = k_thread_name_set(&mcp_message_workers[i], "mcp_msg_worker");
		if (ret != 0) {
			LOG_WRN("Failed to set name for message worker thread %d: %d", i, ret);
		}
	}

	LOG_INF("MCP Server Core started: %d request workers, %d message workers",
		MCP_REQUEST_WORKERS, MCP_MESSAGE_WORKERS);

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
int mcp_server_add_tool(const mcp_tool_record_t *tool_record)
{
	int ret;
	int available_slot = -1;

	if (!tool_record || !tool_record->metadata.name[0] || !tool_record->callback) {
		LOG_ERR("Invalid tool info provided");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry mutex: %d", ret);
		return ret;
	}

	/* Check for duplicate names and find first available slot */
	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		/* Check if slot is empty (available) */
		if (tool_registry.tools[i].metadata.name[0] == '\0' && available_slot == -1) {
			available_slot = i;
		}

		/* Check for duplicate tool name */
		if (tool_registry.tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry.tools[i].metadata.name, tool_record->metadata.name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			LOG_ERR("Tool with name '%s' already exists", tool_record->metadata.name);
			k_mutex_unlock(&tool_registry.registry_mutex);
			return -EEXIST;
		}
	}

	/* Check if registry is full */
	if (available_slot == -1) {
		LOG_ERR("Tool registry is full. Cannot add tool '%s'", tool_record->metadata.name);
		k_mutex_unlock(&tool_registry.registry_mutex);
		return -ENOSPC;
	}

	/* Copy the tool record to the registry */
	memcpy(&tool_registry.tools[available_slot], tool_record, sizeof(mcp_tool_record_t));
	tool_registry.tool_count++;

	LOG_INF("Tool '%s' registered successfully at slot %d", tool_record->metadata.name,
		available_slot);

	k_mutex_unlock(&tool_registry.registry_mutex);

	return 0;
}

int mcp_server_remove_tool(const char *tool_name)
{
	int ret;
	bool found = false;

	if (!tool_name || !tool_name[0]) {
		LOG_ERR("Invalid tool name provided");
		return -EINVAL;
	}

	ret = k_mutex_lock(&tool_registry.registry_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock tool registry mutex: %d", ret);
		return ret;
	}

	/* Find and remove the tool */
	for (int i = 0; i < CONFIG_MCP_MAX_TOOLS; i++) {
		if (tool_registry.tools[i].metadata.name[0] != '\0' &&
		    strncmp(tool_registry.tools[i].metadata.name, tool_name,
			    CONFIG_MCP_TOOL_NAME_MAX_LEN) == 0) {
			/* Clear the tool slot */
			memset(&tool_registry.tools[i], 0, sizeof(mcp_tool_record_t));
			tool_registry.tool_count--;
			found = true;
			LOG_INF("Tool '%s' removed successfully", tool_name);
			break;
		}
	}

	k_mutex_unlock(&tool_registry.registry_mutex);

	if (!found) {
		LOG_ERR("Tool '%s' not found", tool_name);
		return -ENOENT;
	}

	return 0;
}
#endif
