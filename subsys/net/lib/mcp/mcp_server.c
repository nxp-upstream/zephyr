/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>

int mcp_server_init(void)
{
	printk("Hello World from MCP Server Core\n\r");
	return 0;
}
