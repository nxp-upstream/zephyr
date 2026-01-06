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
int mcp_server_http_send(uint32_t client_id, const void *data, size_t length);
int mcp_server_http_disconnect(uint32_t client_id);

#define MCP_SERVER_HTTP_DT_DEFINE(_name) \
	struct mcp_transport_ops _name = {\
		.init = mcp_server_http_init,\
		.send = mcp_server_http_send,\
		.disconnect = mcp_server_http_disconnect,\
	};

#endif /* ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_ */
