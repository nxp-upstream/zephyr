/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <zephyr/net/mcp/mcp_server_http.h>

LOG_MODULE_REGISTER(mcp_sample_hello, LOG_LEVEL_INF);

MCP_SERVER_HTTP_DT_DEFINE(mcp_http_server);
mcp_server_ctx_t server;

/* Tool callback functions */
static int hello_world_tool_callback(const char *params, uint32_t execution_token)
{
	struct mcp_user_message response = {
		.type = MCP_USR_TOOL_RESPONSE,
		.data = "Hello World from tool!",
		.length = strlen("Hello World from tool!")
	};

	printk("Hello World tool executed with params: %s, token: %u\n", params ? params : "none",
	       execution_token);
	mcp_server_submit_tool_message(server, &response, execution_token);
	return 0;
}

static int goodbye_world_tool_callback(const char *params, uint32_t execution_token)
{
	struct mcp_user_message response = {
		.type = MCP_USR_TOOL_RESPONSE,
		.data = "Goodbye World from tool!",
		.length = strlen("Goodbye World from tool!")
	};

	printk("Goodbye World tool executed with params: %s, token: %u\n", params ? params : "none",
	       execution_token);
	mcp_server_submit_tool_message(server, &response, execution_token);
	return 0;
}

/* Tool definitions */
static const struct mcp_tool_record hello_world_tool = {
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
	.callback = hello_world_tool_callback
};

static const struct mcp_tool_record goodbye_world_tool = {
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
	.callback = goodbye_world_tool_callback
};

int main(void)
{
	int ret;

	printk("Hello World\n\r");
	printk("Initializing...\n\r");
	server = mcp_server_init(&mcp_http_server);
	if (server == NULL) {
		printk("MCP Server initialization failed");
		return -ENOMEM;
	}

	printk("Registering Tool #1: Hello world!...\n\r");
	ret = mcp_server_add_tool(server, &hello_world_tool);
	if (ret != 0) {
		printk("Tool #1 registration failed.\n\r");
		return ret;
	}
	printk("Tool #1 registered.\n\r");

	printk("Registering Tool #2: Goodbye world!...\n\r");
	ret = mcp_server_add_tool(server, &goodbye_world_tool);
	if (ret != 0) {
		printk("Tool #2 registration failed.\n\r");
		return ret;
	}
	printk("Tool #2 registered.\n\r");

	printk("Starting...\n\r");
	ret = mcp_server_start(server);
	if (ret != 0) {
		printk("MCP Server start failed: %d\n\r", ret);
		return ret;
	}

	printk("MCP Server running...\n\r");
	return 0;
}
