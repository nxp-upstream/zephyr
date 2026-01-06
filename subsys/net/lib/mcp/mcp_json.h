/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_JSON_H_
#define ZEPHYR_SUBSYS_MCP_JSON_H_

#include "mcp_common.h"

/**
 * @brief Parse JSON-RPC request into MCP message
 *
 * @param json JSON string to parse
 * @param length Length of JSON string
 * @param type Output parameter for message type
 * @param data Output parameter for parsed data (caller must free)
 * @return 0 on success, negative errno on failure
 */
int mcp_json_parse_request(const char *json, size_t length,
			   enum mcp_queue_msg_type *type, void **data);

/**
 * @brief Serialize initialize response to JSON
 *
 * @param resp Response structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Length of serialized JSON on success, negative errno on failure
 */
int mcp_json_serialize_initialize_response(const struct mcp_initialize_response *resp,
					   char *buffer, size_t buffer_size);

/**
 * @brief Serialize tools/list response to JSON
 *
 * @param resp Response structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Length of serialized JSON on success, negative errno on failure
 */
int mcp_json_serialize_tools_list_response(const struct mcp_tools_list_response *resp,
					   char *buffer, size_t buffer_size);

/**
 * @brief Serialize tools/call response to JSON
 *
 * @param resp Response structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Length of serialized JSON on success, negative errno on failure
 */
int mcp_json_serialize_tools_call_response(const struct mcp_tools_call_response *resp,
					   char *buffer, size_t buffer_size);

/**
 * @brief Serialize error response to JSON
 *
 * @param resp Error response structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Length of serialized JSON on success, negative errno on failure
 */
int mcp_json_serialize_error_response(const struct mcp_error_response *resp,
					  char *buffer, size_t buffer_size);

#endif /* ZEPHYR_SUBSYS_MCP_JSON_H_ */
