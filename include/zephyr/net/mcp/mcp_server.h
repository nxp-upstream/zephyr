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
	MCP_USR_TOOL_RESPONSE,
	MCP_USR_TOOL_NOTIFICATION,
	MCP_USR_TOOL_CANCEL_ACK,
	MCP_USR_TOOL_PING,
	MCP_USR_GENERIC_RESPONSE
} mcp_app_msg_type_t;

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

typedef struct mcp_user_message {
	mcp_app_msg_type_t type;
	int length;
	void *data;
} mcp_app_message_t;

/*
 * @brief Server context handle
 */
typedef void* mcp_server_ctx_t;

/*
 * @brief Transport operations structure for MCP server communication.
 */
struct mcp_transport_ops {
	/**
	 * @brief Initialize the transport mechanism
	 * @return 0 on success, negative errno on failure
	 */
	int (*init)(mcp_server_ctx_t server_ctx);

	/**
	 * @brief Send data to a client
	 * @param client_id Client identifier
	 * @param data Data buffer to send
	 * @param length Data length
	 * @return 0 on success, negative errno on failure
	 */
	int (*send)(uint32_t client_id, const void *data, size_t length);

	/*
	 * @brief Disconnect a client
	 * @param client_id Client identifier
	 * @return 0 on success, negative errno on failure
	 */
	int (*disconnect)(uint32_t client_id);
};

/**
 * @brief Initialize the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
mcp_server_ctx_t mcp_server_init(struct mcp_transport_ops *transport_ops);

/**
 * @brief Start the MCP Server
 *
 * @return 0 on success, negative errno on failure
 */
int mcp_server_start(mcp_server_ctx_t server_ctx);

int mcp_server_submit_tool_message(mcp_server_ctx_t server_ctx, const mcp_app_message_t *user_msg, uint32_t execution_token);
/**
 * @brief Submit an application message (response/notification)
 *
 * @param user_msg Application message to submit
 * @param execution_token Execution token for tracking
 * @return 0 on success, negative errno on failure
 */
int mcp_server_submit_app_message(mcp_server_ctx_t server_ctx, const mcp_app_message_t *user_msg, uint32_t execution_token);

/**
 * @brief Add a tool to the server
 *
 * @param tool_record Tool definition with metadata and callback
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool_record
 * @retval -EEXIST Tool name already exists
 * @retval -ENOSPC Registry full
 */
int mcp_server_add_tool(mcp_server_ctx_t server_ctx, const mcp_tool_record_t *tool_record);

/**
 * @brief Remove a tool from the server
 *
 * @param tool_name Name of tool to remove
 * @return 0 on success, negative errno on failure
 * @retval -EINVAL Invalid tool name
 * @retval -ENOENT Tool not found
 */
int mcp_server_remove_tool(mcp_server_ctx_t server_ctx, const char *tool_name);

int mcp_server_is_execution_canceled(mcp_server_ctx_t server_ctx, uint32_t execution_token, bool *is_canceled);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MCP_SERVER_H_ */
