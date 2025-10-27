/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_MCP_SERVER_H_
#define ZEPHYR_INCLUDE_NET_MCP_SERVER_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mcp_message_msg {
	uint32_t token;
	/* More fields will be added later */
};

/**
 * @brief Initialize the MCP Server.
 *
 */
int mcp_server_init(void);

/**
 * @brief Start the MCP Server.
 *
 */
int mcp_server_start(void);

/**
 * @brief Queues a response to the MCP Server library, which takes care of sending it to
 * the MCP Client.
 *
 */
int mcp_queue_response(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MCP_SERVER_H_ */
