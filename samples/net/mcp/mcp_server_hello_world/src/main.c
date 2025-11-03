/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>

LOG_MODULE_REGISTER(mcp_sample_hello, LOG_LEVEL_INF);

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/* Tool callback functions */
static int hello_world_tool_callback(const char *params, uint32_t execution_token)
{
	printk("Hello World tool executed with params: %s, token: %u\n", params ? params : "none",
	       execution_token);
	return 0;
}

static int goodbye_world_tool_callback(const char *params, uint32_t execution_token)
{
	printk("Goodbye World tool executed with params: %s, token: %u\n", params ? params : "none",
	       execution_token);
	return 0;
}

/* Tool definitions */
static const mcp_tool_record_t hello_world_tool = {
	.metadata = {
			.name = "hello_world",
			.input_schema = "{\"type\":\"object\",\"properties\":{\"message\":{"
					"\"type\":\"string\"}}}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "A simple hello world greeting tool",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Hello World Tool",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			.output_schema = "{\"type\":\"object\",\"properties\":{\"response\":{"
					 "\"type\":\"string\"}}}",
#endif
		},
	.callback = hello_world_tool_callback};

static const mcp_tool_record_t goodbye_world_tool = {
	.metadata = {
			.name = "goodbye_world",
			.input_schema = "{\"type\":\"object\",\"properties\":{\"message\":{"
					"\"type\":\"string\"}}}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "A simple goodbye world farewell tool",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Goodbye World Tool",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			.output_schema = "{\"type\":\"object\",\"properties\":{\"response\":{"
					 "\"type\":\"string\"}}}",
#endif
		},
	.callback = goodbye_world_tool_callback};
#endif /* CONFIG_MCP_TOOLS_CAPABILITY */

int main(void)
{
	int ret;

	printk("Hello World\n\r");
	printk("Initializing...\n\r");
	ret = mcp_server_init();
	if (ret != 0) {
		printk("MCP Server initialization failed: %d\n\r", ret);
		return ret;
	}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	printk("Registering Tool #1: Hello world!...\n\r");
	ret = mcp_server_add_tool(&hello_world_tool);
	if (ret != 0) {
		printk("Tool #1 registration failed.\n\r");
		return ret;
	}
	printk("Tool #1 registered.\n\r");

	printk("Registering Tool #2: Goodbye world!...\n\r");
	ret = mcp_server_add_tool(&goodbye_world_tool);
	if (ret != 0) {
		printk("Tool #2 registration failed.\n\r");
		return ret;
	}
	printk("Tool #2 registered.\n\r");
#else
	printk("MCP Tools capability not enabled - skipping tool registration\n\r");
#endif /* CONFIG_MCP_TOOLS_CAPABILITY */

	printk("Starting...\n\r");
	ret = mcp_server_start();
	if (ret != 0) {
		printk("MCP Server start failed: %d\n\r", ret);
		return ret;
	}

	printk("MCP Server running...\n\r");
	return 0;
}
