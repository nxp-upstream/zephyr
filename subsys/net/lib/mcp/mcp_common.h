/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_COMMON_H_
#define ZEPHYR_SUBSYS_MCP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>

#define MCP_MAX_REQUESTS (CONFIG_HTTP_SERVER_MAX_CLIENTS * CONFIG_HTTP_SERVER_MAX_STREAMS)

typedef enum {
	MCP_NOTIF_INITIALIZED
} mcp_notification_method_type_t;

typedef enum {
	MCP_MSG_SYSTEM,
	MCP_MSG_REQUEST_INITIALIZE,
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	MCP_MSG_REQUEST_TOOLS_LIST,
	MCP_MSG_REQUEST_TOOLS_CALL,
#endif
	MCP_MSG_RESPONSE_INITIALIZE,
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	MCP_MSG_RESPONSE_TOOLS_LIST,
	MCP_MSG_RESPONSE_TOOLS_CALL,
#endif
	MCP_MSG_ERROR_INITIALIZE,
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	MCP_MSG_ERROR_TOOLS_LIST,
	MCP_MSG_ERROR_TOOLS_CALL,
#endif
	MCP_MSG_NOTIFICATION,
} mcp_queue_msg_type_t;

typedef enum {
	MCP_SYS_CLIENT_SHUTDOWN,
	MCP_SYS_CANCEL
} mcp_system_msg_type_t;

typedef enum {
	MCP_PROMPTS = 0x1,
	MCP_RESOURCES = 0x2,
	MCP_TOOLS = 0x4,
	MCP_LOGGING = 0x8,
	MCP_COMPLETION = 0x10,
	MCP_PAGINATION = 0x20
} mcp_server_capabilities_t;

typedef enum {
	MCP_EXEC_ACTIVE,
	MCP_EXEC_CANCELED,
	MCP_EXEC_FINISHED
} mcp_execution_state_t;

typedef enum {
	/* JSON-RPC 2.0 standard error codes */
	MCP_ERROR_PARSE_ERROR = -32700,
	MCP_ERROR_INVALID_REQUEST = -32600,
	MCP_ERROR_METHOD_NOT_FOUND = -32601,
	MCP_ERROR_INVALID_PARAMS = -32602,
	MCP_ERROR_INTERNAL_ERROR = -32603,
	MCP_ERROR_SERVER_ERROR = -32000,
} mcp_error_code_t;

typedef struct mcp_system_msg {
	mcp_system_msg_type_t type;
	uint32_t request_id;
	uint32_t client_id;
} mcp_system_msg_t;

typedef struct mcp_error_response {
	uint32_t request_id;
	int32_t error_code;
	char error_message[128];
} mcp_error_response_t;

typedef struct mcp_initialize_request {
	uint32_t request_id;
	uint32_t client_id;
} mcp_initialize_request_t;

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
typedef struct mcp_tools_list_request {
	uint32_t request_id;
	uint32_t client_id;
} mcp_tools_list_request_t;

typedef struct mcp_tools_call_request {
	uint32_t request_id;
	uint32_t client_id;
	char name[CONFIG_MCP_TOOL_NAME_MAX_LEN];
	char arguments[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN];
} mcp_tools_call_request_t;
#endif

typedef struct mcp_initialize_response {
	uint32_t request_id;
	uint32_t capabilities;
} mcp_initialize_response_t;

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
typedef struct mcp_tools_list_response {
	uint32_t request_id;
	uint8_t tool_count;
	mcp_tool_metadata_t tools[CONFIG_MCP_MAX_TOOLS];
} mcp_tools_list_response_t;

typedef struct mcp_tools_call_response {
	uint32_t request_id;
	int length;
	char result[CONFIG_MCP_TOOL_RESULT_MAX_LEN];
} mcp_tools_call_response_t;
#endif

typedef struct mcp_client_notification {
	uint32_t client_id;
	mcp_notification_method_type_t method;
} mcp_client_notification_t;

typedef struct mcp_server_notification {
	mcp_notification_method_type_t method;
} mcp_server_notification_t;

typedef struct mcp_request_queue_msg {
	mcp_queue_msg_type_t type;
	void *data;
} mcp_request_queue_msg_t;

typedef struct mcp_transport_queue_msg {
	mcp_queue_msg_type_t type;
	void *data;
} mcp_transport_queue_msg_t;

typedef struct mcp_response_queue_msg {
	mcp_queue_msg_type_t type;
	void *data;
} mcp_response_queue_msg_t;

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
typedef struct {
	mcp_tool_record_t tools[CONFIG_MCP_MAX_TOOLS];
	struct k_mutex registry_mutex;
	uint8_t tool_count;
} mcp_tool_registry_t;

typedef struct {
	uint32_t execution_token;
	uint32_t request_id;
	uint32_t client_id;
	k_tid_t worker_id;
	int64_t start_timestamp;
	int64_t cancel_timestamp;
	int64_t last_message_timestamp;
	bool worker_released;
	mcp_execution_state_t execution_state;
} mcp_execution_context_t;

typedef struct {
	mcp_execution_context_t executions[MCP_MAX_REQUESTS];
	struct k_mutex registry_mutex;
} mcp_execution_registry_t;
#endif

void *mcp_alloc(size_t size);
void mcp_free(void *ptr);

#endif /* ZEPHYR_SUBSYS_MCP_COMMON_H_ */
