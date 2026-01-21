/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_MCP_SERVER_H_
#define ZEPHYR_INCLUDE_NET_MCP_SERVER_H_

/**
 * @file
 * @brief Model Context Protocol (MCP) Server API
 */

#include <zephyr/kernel.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mcp_app_msg_type {
	MCP_USR_TOOL_RESPONSE,
	MCP_USR_TOOL_NOTIFICATION,
	MCP_USR_TOOL_CANCEL_ACK,
	MCP_USR_TOOL_PING
};

/**
 * @brief Tool metadata structure
 */
struct mcp_tool_metadata {
	char name[CONFIG_MCP_TOOL_NAME_MAX_LEN];
	char input_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN];
#ifdef CONFIG_MCP_TOOL_DESC
	char description[CONFIG_MCP_TOOL_DESC_MAX_LEN];
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
	char title[CONFIG_MCP_TOOL_NAME_MAX_LEN];
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
	char output_schema[CONFIG_MCP_TOOL_SCHEMA_MAX_LEN];
#endif
};

/**
 * @brief Server context handle
 */
typedef void *mcp_server_ctx_t;

/**
 * @brief Tool callback function
 *
 * @param params JSON string with tool parameters
 * @param execution_token Unique execution identifier
 * @return 0 on success, negative errno on failure
 */
typedef int (*mcp_tool_callback_t)(const char *params, uint32_t execution_token);

/**
 * @brief Tool definition structure
 */
struct mcp_tool_record {
	struct mcp_tool_metadata metadata;
	mcp_tool_callback_t callback;
};

/**
 * @brief Message sent from a tool (response/notification)
 */
struct mcp_tool_message {
	enum mcp_app_msg_type type;
	int length;
	void *data;
};

/**
 * @brief Initialize the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
mcp_server_ctx_t mcp_server_init(void);

/**
 * @brief Start the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
int mcp_server_start(mcp_server_ctx_t server_ctx);

/**
 * @brief Submit a message from a tool (response/notification)
 *
 * @param user_msg Application message to submit
 * @param execution_token Execution token for tracking
 * @return 0 on success, negative errno on failure
 */
int mcp_server_submit_tool_message(mcp_server_ctx_t server_ctx,
				   const struct mcp_tool_message *user_msg,
				   uint32_t execution_token);

/**
 * @brief Add a tool to the server
 *
 * @param tool_record Tool definition with metadata and callback
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool_record
 * @retval -EEXIST Tool name already exists
 * @retval -ENOSPC Registry full
 */
int mcp_server_add_tool(mcp_server_ctx_t server_ctx, const struct mcp_tool_record *tool_record);

/**
 * @brief Remove a tool from the server
 *
 * @param tool_name Name of tool to remove
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool name
 * @retval -ENOENT Tool not found
 */
int mcp_server_remove_tool(mcp_server_ctx_t server_ctx, const char *tool_name);

/**
 * @brief Helper for checking the execution state of a tool
 *
 * @param execution_token Token representing the execution
 * @param is_canceled Pointer to store cancellation state
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool name
 * @retval -ENOENT Tool not found
 */
int mcp_server_is_execution_canceled(mcp_server_ctx_t server_ctx, uint32_t execution_token,
				     bool *is_canceled);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MCP_SERVER_H_ */
