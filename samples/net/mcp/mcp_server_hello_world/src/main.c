/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>

LOG_MODULE_REGISTER(mcp_sample_hello, LOG_LEVEL_INF);

int main(void)
{
	printk("Hello World\n\r");
	printk("Initializing...\n\r");
	mcp_server_init();
	printk("Starting...\n\r");
	mcp_server_start();
	return 0;
}
