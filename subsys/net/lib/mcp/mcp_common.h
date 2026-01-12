/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_COMMON_H_
#define ZEPHYR_SUBSYS_MCP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

// enum mcp_notification_method_type {
// 	MCP_NOTIF_INITIALIZED,
// 	MCP_NOTIF_CANCELLED,
// 	MCP_NOTIF_PROGRESS
// };

// enum mcp_queue_msg_type {
// 	MCP_MSG_SYSTEM,
// 	MCP_MSG_REQUEST_INITIALIZE,
// 	MCP_MSG_REQUEST_TOOLS_LIST,
// 	MCP_MSG_REQUEST_TOOLS_CALL,
// 	MCP_MSG_RESPONSE_INITIALIZE,
// 	MCP_MSG_RESPONSE_TOOLS_LIST,
// 	MCP_MSG_RESPONSE_TOOLS_CALL,
// 	MCP_MSG_ERROR_INITIALIZE,
// 	MCP_MSG_ERROR_TOOLS_LIST,
// 	MCP_MSG_ERROR_TOOLS_CALL,
// 	MCP_MSG_NOTIFICATION,
// 	MCP_MSG_NOTIF_CANCELLED,
// 	MCP_MSG_NOTIF_PROGRESS,
// 	MCP_MSG_UNKNOWN,
// };

// enum mcp_system_msg_type {
// 	MCP_SYS_CLIENT_SHUTDOWN,
// 	MCP_SYS_CANCEL
// };

// enum mcp_server_capabilities {
// 	MCP_PROMPTS = 0x1,
// 	MCP_RESOURCES = 0x2,
// 	MCP_TOOLS = 0x4,
// 	MCP_LOGGING = 0x8,
// 	MCP_COMPLETION = 0x10,
// 	MCP_PAGINATION = 0x20
// };

// enum mcp_error_code {
// 	/* JSON-RPC 2.0 standard error codes */
// 	MCP_ERROR_PARSE_ERROR = -32700,
// 	MCP_ERROR_INVALID_REQUEST = -32600,
// 	MCP_ERROR_METHOD_NOT_FOUND = -32601,
// 	MCP_ERROR_INVALID_PARAMS = -32602,
// 	MCP_ERROR_INTERNAL_ERROR = -32603,
// 	MCP_ERROR_SERVER_ERROR = -32000,
// };

/*******************************************************************************
 * Requests
 ******************************************************************************/
// struct mcp_system_msg {
// 	enum mcp_system_msg_type type;
// 	uint32_t request_id;
// 	uint32_t client_id;
// };

// struct mcp_client_notification {
// 	uint32_t client_id;
// 	enum mcp_notification_method_type method;
// };

// struct mcp_cancelled_notification {
// 	uint32_t request_id;
// 	char reason[128];
// };

// struct mcp_progress_notification {
// 	char progress_token[64];
// 	uint32_t progress;
// 	uint32_t total;
// 	char message[256];
// };

// struct mcp_initialize_request {
// 	uint32_t request_id;
// 	uint32_t client_id;
// };

// struct mcp_tools_list_request {
// 	uint32_t request_id;
// 	uint32_t client_id;
// };

// struct mcp_tools_call_request {
// 	uint32_t request_id;
// 	uint32_t client_id;
// 	char name[CONFIG_MCP_TOOL_NAME_MAX_LEN];
// 	char arguments[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN];
// };

/*******************************************************************************
 * Responses
 ******************************************************************************/
// struct mcp_server_notification {
// 	enum mcp_notification_method_type method;
// };

// struct mcp_initialize_response {
// 	uint32_t request_id;
// 	uint32_t capabilities;
// };

// struct mcp_error_response {
// 	uint32_t request_id;
// 	int32_t error_code;
// 	char error_message[128];
// };

// struct mcp_tools_list_response {
// 	uint32_t request_id;
// 	uint8_t tool_count;
// 	struct mcp_tool_metadata tools[CONFIG_MCP_MAX_TOOLS];
// };

// struct mcp_tools_call_response {
// 	uint32_t request_id;
// 	int length;
// 	char result[CONFIG_MCP_TOOL_RESULT_MAX_LEN];
// };

void *mcp_alloc(size_t size);
void mcp_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MCP_COMMON_H_ */
