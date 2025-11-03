/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_MCP_SERVER_H_
#define ZEPHYR_INCLUDE_NET_MCP_SERVER_H_

#include <zephyr/kernel.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
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

/* Tool callback function signature */
typedef int (*mcp_tool_callback_t)(const char *params, uint32_t execution_token);

/* Tool definition structure */
typedef struct mcp_tool_record {
	mcp_tool_metadata_t metadata;
	mcp_tool_callback_t callback;
} mcp_tool_record_t;
#endif

struct mcp_message_msg {
	uint32_t token;
	/* More fields will be added later */
};

/**
 * @brief Initialize the MCP Server.
 *
 * @return 0 on success, negative errno code on failure
 */
int mcp_server_init(void);

/**
 * @brief Start the MCP Server.
 *
 * @return 0 on success, negative errno code on failure
 */
int mcp_server_start(void);

/**
 * @brief Queues a response to the MCP Server library, which takes care of sending it to
 * the MCP Client.
 *
 * @return 0 on success, negative errno code on failure
 */
int mcp_queue_response(void);

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/**
 * @brief Add a tool to the MCP server tool registry
 *
 * @param tool_record Pointer to tool record containing metadata and callback
 * @return 0 on success, negative errno code on failure
 *         -EINVAL if tool_record is NULL or invalid
 *         -EEXIST if tool with same name already exists
 *         -ENOSPC if tool registry is full
 */
int mcp_server_add_tool(const mcp_tool_record_t *tool_record);

/**
 * @brief Remove a tool from the MCP server tool registry
 *
 * @param tool_name Name of the tool to remove
 * @return 0 on success, negative errno code on failure
 *         -EINVAL if tool_name is NULL
 *         -ENOENT if tool not found
 */
int mcp_server_remove_tool(const char *tool_name);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MCP_SERVER_H_ */
