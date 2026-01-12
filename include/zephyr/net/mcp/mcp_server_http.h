/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_
#define ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_

/**
 * @file
 * @brief Model Context Protocol (MCP) Server HTTP API
 */

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>

int mcp_server_http_init(mcp_server_ctx_t server_ctx);
int mcp_server_http_start(mcp_server_ctx_t server_ctx);

#endif /* ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_ */
