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

typedef enum {
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	MCP_USR_TOOL_RESPONSE,
	MCP_USR_TOOL_NOTIFICATION,
#endif
	MCP_USR_GENERIC_RESPONSE
} mcp_app_msg_type_t;

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/**
 * @brief Tool metadata structure
 */
typedef struct mcp_tool_metadata {
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
} mcp_tool_metadata_t;

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
typedef struct mcp_tool_record {
	mcp_tool_metadata_t metadata;
	mcp_tool_callback_t callback;
} mcp_tool_record_t;
#endif

typedef struct mcp_user_message {
	mcp_app_msg_type_t type;
	int length;
	void *data;
} mcp_app_message_t;

/**
 * @brief Initialize the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
int mcp_server_init(void);

/**
 * @brief Start the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
int mcp_server_start(void);

int mcp_server_submit_app_message(const mcp_app_message_t *user_msg, uint32_t execution_token);

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/**
 * @brief Add a tool to the server
 *
 * @param tool_record Tool definition with metadata and callback
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool_record
 * @retval -EEXIST Tool name already exists
 * @retval -ENOSPC Registry full
 */
int mcp_server_add_tool(const mcp_tool_record_t *tool_record);

/**
 * @brief Remove a tool from the server
 *
 * @param tool_name Name of tool to remove
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool name
 * @retval -ENOENT Tool not found
 */
int mcp_server_remove_tool(const char *tool_name);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MCP_SERVER_H_ */
