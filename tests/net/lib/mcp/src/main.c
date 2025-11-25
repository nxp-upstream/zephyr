/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>
#include <errno.h>

#include "mcp_common.h"
#include "mcp_transport.h"

extern struct k_msgq mcp_request_queue;
extern struct k_msgq mcp_message_queue;
extern int mcp_transport_queue_call_count;
extern mcp_transport_queue_msg_t mcp_transport_last_queued_msg;
/* Tool execution test callbacks with different behaviors */
static int tool_execution_count;
static char last_execution_params[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN];
static uint32_t last_execution_token;

#ifdef CONFIG_ZTEST
extern uint8_t mcp_server_get_client_count(void);
extern uint8_t mcp_server_get_tool_count(void);
#endif

#define CLIENT_ID_BASE               1000
#define CLIENT_ID_LIFECYCLE_TEST     (CLIENT_ID_BASE + 1)
#define CLIENT_ID_INITIALIZE_TEST    (CLIENT_ID_BASE + 2)
#define CLIENT_ID_EDGE_CASE_TEST     (CLIENT_ID_BASE + 3)
#define CLIENT_ID_SHUTDOWN_TEST      (CLIENT_ID_BASE + 4)
#define CLIENT_ID_INVALID_STATE_TEST (CLIENT_ID_BASE + 5)
#define CLIENT_ID_MULTI_CLIENT_1     (CLIENT_ID_BASE + 6)
#define CLIENT_ID_MULTI_CLIENT_2     (CLIENT_ID_BASE + 7)
#define CLIENT_ID_MULTI_CLIENT_3     (CLIENT_ID_BASE + 8)
#define CLIENT_ID_MULTI_CLIENT_4     (CLIENT_ID_BASE + 9)
#define CLIENT_ID_UNREGISTERED       (CLIENT_ID_BASE + 999)

#define REQUEST_ID_BASE 2000

#define REQ_ID_EDGE_CASE_UNREGISTERED (REQUEST_ID_BASE + 1)
#define REQ_ID_EDGE_CASE_INITIALIZE   (REQUEST_ID_BASE + 2)
#define REQ_ID_EDGE_CASE_TOOLS_LIST   (REQUEST_ID_BASE + 3)

#define REQ_ID_INITIALIZE_TEST (REQUEST_ID_BASE + 10)

#define REQ_ID_LIFECYCLE_INITIALIZE  (REQUEST_ID_BASE + 20)
#define REQ_ID_LIFECYCLE_TOOLS_INIT  (REQUEST_ID_BASE + 21)
#define REQ_ID_LIFECYCLE_TOOLS_READY (REQUEST_ID_BASE + 22)

#define REQ_ID_SHUTDOWN_INITIALIZE   (REQUEST_ID_BASE + 30)
#define REQ_ID_SHUTDOWN_TOOLS_ACTIVE (REQUEST_ID_BASE + 31)
#define REQ_ID_SHUTDOWN_TOOLS_DEAD   (REQUEST_ID_BASE + 32)

#define REQ_ID_INVALID_INITIALIZE   (REQUEST_ID_BASE + 40)
#define REQ_ID_INVALID_REINITIALIZE (REQUEST_ID_BASE + 41)

#define REQ_ID_MULTI_CLIENT_1_INIT   (REQUEST_ID_BASE + 50)
#define REQ_ID_MULTI_CLIENT_2_INIT   (REQUEST_ID_BASE + 51)
#define REQ_ID_MULTI_CLIENT_3_INIT   (REQUEST_ID_BASE + 52)
#define REQ_ID_MULTI_CLIENT_4_INIT_1 (REQUEST_ID_BASE + 53)
#define REQ_ID_MULTI_CLIENT_4_INIT_2 (REQUEST_ID_BASE + 54)

#define MCP_MSG_INVALID_TYPE 0xFF

static void send_tools_call_request(uint32_t client_id, uint32_t request_id, const char *tool_name,
				    const char *arguments)
{
	int ret;
	mcp_request_queue_msg_t msg;
	mcp_tools_call_request_t *tools_req;

	tools_req = (mcp_tools_call_request_t *)mcp_alloc(sizeof(mcp_tools_call_request_t));
	zassert_not_null(tools_req, "Tools call request allocation failed");

	tools_req->request_id = request_id;
	tools_req->client_id = client_id;

	strncpy(tools_req->name, tool_name, CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
	tools_req->name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

	if (arguments) {
		strncpy(tools_req->arguments, arguments, CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1);
		tools_req->arguments[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1] = '\0';
	} else {
		tools_req->arguments[0] = '\0';
	}

	msg.type = MCP_MSG_REQUEST_TOOLS_CALL;
	msg.data = tools_req;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Tools call request queueing failed");

	k_msleep(200);
}

static void send_initialize_request(uint32_t client_id, uint32_t request_id)
{
	int ret;
	mcp_request_queue_msg_t msg;
	mcp_initialize_request_t *init_req;

	init_req = (mcp_initialize_request_t *)mcp_alloc(sizeof(mcp_initialize_request_t));
	zassert_not_null(init_req, "Initialize request allocation failed");

	init_req->request_id = request_id;
	init_req->client_id = client_id;

	msg.type = MCP_MSG_REQUEST_INITIALIZE;
	msg.data = init_req;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Initialize request queueing failed");

	k_msleep(50);
}

static void send_client_shutdown(uint32_t client_id)
{
	int ret;
	mcp_request_queue_msg_t msg;
	mcp_system_msg_t *sys_msg;

	sys_msg = (mcp_system_msg_t *)mcp_alloc(sizeof(mcp_system_msg_t));
	zassert_not_null(sys_msg, "System message allocation failed");

	sys_msg->type = MCP_SYS_CLIENT_SHUTDOWN;
	sys_msg->client_id = client_id;

	msg.type = MCP_MSG_SYSTEM;
	msg.data = sys_msg;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Shutdown message queueing failed");

	k_msleep(50);
}

static void send_initialized_notification(uint32_t client_id)
{
	int ret;
	mcp_request_queue_msg_t msg;
	mcp_client_notification_t *notification;

	notification = (mcp_client_notification_t *)mcp_alloc(sizeof(mcp_client_notification_t));
	zassert_not_null(notification, "Notification allocation failed");

	notification->client_id = client_id;
	notification->method = MCP_NOTIF_INITIALIZED;

	msg.type = MCP_MSG_NOTIFICATION;
	msg.data = notification;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Notification queueing failed");

	k_msleep(50);
}

static void send_tools_list_request(uint32_t client_id, uint32_t request_id)
{
	int ret;
	mcp_request_queue_msg_t msg;
	mcp_tools_list_request_t *tools_req;

	tools_req = (mcp_tools_list_request_t *)mcp_alloc(sizeof(mcp_tools_list_request_t));
	zassert_not_null(tools_req, "Tools request allocation failed");

	tools_req->request_id = request_id;
	tools_req->client_id = client_id;

	msg.type = MCP_MSG_REQUEST_TOOLS_LIST;
	msg.data = tools_req;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Tools request queueing failed");

	k_msleep(50);
}

static void initialize_client_fully(uint32_t client_id, uint32_t request_id)
{
	send_initialize_request(client_id, request_id);
	send_initialized_notification(client_id);
}

static void reset_transport_mock(void)
{
	mcp_transport_queue_call_count = 0;
	memset(&mcp_transport_last_queued_msg, 0, sizeof(mcp_transport_last_queued_msg));
}
static int stub_tool_callback_1(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[] = "{"
			     "\"content\": ["
			     "{"
			     "\"type\": \"text\","
			     "\"text\": \"Hello world from callback 1. This tool processed the "
			     "request successfully.\""
			     "}"
			     "],"
			     "\"isError\": false"
			     "}";

	printk("Stub tool 1 executed - Token: %u, Args: %s\n", execution_token,
	       params ? params : "(null)");

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from callback 1: %d\n", ret);
		return ret;
	}

	return 0;
}

static int stub_tool_callback_2(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[] = "{"
			     "\"content\": ["
			     "{"
			     "\"type\": \"text\","
			     "\"text\": \"Hello world from callback 2. Tool execution completed.\""
			     "}"
			     "],"
			     "\"isError\": false"
			     "}";

	printk("Stub tool 2 executed - Token: %u, Args: %s\n", execution_token,
	       params ? params : "(null)");

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from callback 2: %d\n", ret);
		return ret;
	}

	return 0;
}

static int stub_tool_callback_3(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[] =
		"{"
		"\"content\": ["
		"{"
		"\"type\": \"text\","
		"\"text\": \"Hello world from callback 3. Registry tool execution successful.\""
		"}"
		"],"
		"\"isError\": false"
		"}";

	printk("Stub tool 3 executed - Token: %u, Args: %s\n", execution_token,
	       params ? params : "(null)");

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from callback 3: %d\n", ret);
		return ret;
	}

	return 0;
}

static int test_tool_success_callback(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[512];
	char text_content[256];

	tool_execution_count++;
	last_execution_token = execution_token;

	if (params) {
		strncpy(last_execution_params, params, sizeof(last_execution_params) - 1);
		last_execution_params[sizeof(last_execution_params) - 1] = '\0';
	}

	snprintf(text_content, sizeof(text_content),
		 "Success tool executed successfully. Execution count: %d. Input parameters: %s",
		 tool_execution_count, params ? params : "none");

	snprintf(result_data, sizeof(result_data),
		 "{"
		 "\"content\": ["
		 "{"
		 "\"type\": \"text\","
		 "\"text\": \"%s\""
		 "}"
		 "],"
		 "\"isError\": false"
		 "}",
		 text_content);

	printk("SUCCESS tool executed! Count: %d, Token: %u, Args: %s\n", tool_execution_count,
	       execution_token, params ? params : "(null)");

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from success callback: %d\n", ret);
		return ret;
	}

	return 0;
}

static int test_tool_error_callback(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[] = "{"
			     "\"content\": ["
			     "{"
			     "\"type\": \"text\","
			     "\"text\": \"Error: This tool intentionally failed to test error "
			     "handling. The operation could not be completed.\""
			     "}"
			     "],"
			     "\"isError\": true"
			     "}";

	tool_execution_count++;

	printk("ERROR tool executed! Count: %d, Token: %u, Args: %s (submitting error response)\n",
	       tool_execution_count, execution_token, params ? params : "(null)");

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from error callback: %d\n", ret);
		return ret;
	}

	return 0;
}

static int test_tool_slow_callback(const char *params, uint32_t execution_token)
{
	int ret;
	mcp_app_message_t response;
	char result_data[] = "{"
			     "\"content\": ["
			     "{"
			     "\"type\": \"text\","
			     "\"text\": \"Slow operation completed successfully. The task took "
			     "100ms to simulate a long-running operation.\""
			     "}"
			     "],"
			     "\"isError\": false"
			     "}";

	tool_execution_count++;

	printk("SLOW tool starting execution! Token: %u\n", execution_token);
	k_msleep(100); /* Simulate slow operation */
	printk("SLOW tool completed execution! Token: %u\n", execution_token);

	response.type = MCP_USR_TOOL_RESPONSE;
	response.data = result_data;
	response.length = strlen(result_data);

	ret = mcp_server_submit_app_message(&response, execution_token);
	if (ret != 0) {
		printk("Failed to submit response from slow callback: %d\n", ret);
		return ret;
	}

	return 0;
}
/* Reset tool execution tracking */
static void reset_tool_execution_tracking(void)
{
	tool_execution_count = 0;
	last_execution_token = 0;
	memset(last_execution_params, 0, sizeof(last_execution_params));
}

/* Register test tools for comprehensive testing */
static void register_test_tools(void)
{
	int ret;

	mcp_tool_record_t success_tool = {
		.metadata = {
				.name = "test_success_tool",
				.input_schema = "{\"type\":\"object\",\"properties\":{\"message\":{"
						"\"type\":\"string\"}}}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool that always succeeds",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
				.title = "Success Test Tool",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
				.output_schema = "{\"type\":\"object\",\"properties\":{\"result\":{"
						 "\"type\":\"string\"}}}",
#endif
			},
		.callback = test_tool_success_callback
	};

	mcp_tool_record_t error_tool = {
		.metadata = {
				.name = "test_error_tool",
				.input_schema = "{\"type\":\"object\"}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool that always returns error",
#endif
			},
		.callback = test_tool_error_callback
	};

	mcp_tool_record_t slow_tool = {
		.metadata = {
				.name = "test_slow_tool",
				.input_schema = "{\"type\":\"object\"}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool that takes time to execute",
#endif
			},
		.callback = test_tool_slow_callback
	};

	ret = mcp_server_add_tool(&success_tool);
	zassert_equal(ret, 0, "Success tool should register");

	ret = mcp_server_add_tool(&error_tool);
	zassert_equal(ret, 0, "Error tool should register");

	ret = mcp_server_add_tool(&slow_tool);
	zassert_equal(ret, 0, "Slow tool should register");
}

/* Clean up test tools */
static void cleanup_test_tools(void)
{
	mcp_server_remove_tool("test_success_tool");
	mcp_server_remove_tool("test_error_tool");
	mcp_server_remove_tool("test_slow_tool");
}

ZTEST(mcp_server_tests, test_tools_call_comprehensive)
{
	uint8_t initial_tool_count = mcp_server_get_tool_count();
	int initial_transport_count;

	reset_tool_execution_tracking();
	reset_transport_mock();

	/* Register test tools */
	register_test_tools();
	zassert_equal(mcp_server_get_tool_count(), initial_tool_count + 3,
		      "All test tools should be registered");

	/* Initialize client */
	initialize_client_fully(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_INITIALIZE);

	/* Test 1: Successful tool execution with parameters */
	printk("=== Test 1: Successful tool execution ===\n");
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3001, "test_success_tool",
				"{\"message\":\"hello world\"}");

	/* Verify tool executed AND response was submitted to transport */
	zassert_equal(tool_execution_count, 1, "Success tool should execute once");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tool response should be submitted to transport");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_CALL,
		      "Transport should receive tools/call response");

	/* Verify response content contains expected data */
	mcp_tools_call_response_t *response =
		(mcp_tools_call_response_t *)mcp_transport_last_queued_msg.data;
	zassert_not_null(response, "Response data should not be NULL");
	zassert_equal(response->request_id, 3001, "Response should have correct request ID");
	zassert_true(strstr(response->result, "Success tool executed successfully") != NULL,
		     "Response should contain success message");

	/* Test 2: Tool execution with empty parameters */
	printk("=== Test 2: Tool execution with empty parameters ===\n");
	initial_transport_count = mcp_transport_queue_call_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3002, "test_success_tool", "");

	zassert_equal(tool_execution_count, 2, "Tool should execute twice total");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Second tool response should be submitted");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_CALL,
		      "Transport should receive second tools/call response");

	/* Test 3: Tool execution with NULL parameters */
	printk("=== Test 3: Tool execution with NULL parameters ===\n");
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3003, "test_success_tool", NULL);

	zassert_equal(tool_execution_count, 3, "Tool should execute three times total");
	zassert_equal(mcp_transport_queue_call_count, 1, "Third tool response should be submitted");

	/* Test 4: Tool that returns error */
	printk("=== Test 4: Tool that returns error ===\n");
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3004, "test_error_tool",
				"{\"test\":\"data\"}");

	/* Error tool should still execute and submit response */
	zassert_equal(tool_execution_count, 4, "Error tool should still execute");
	zassert_equal(mcp_transport_queue_call_count, 1, "Error tool response should be submitted");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_CALL,
		      "Transport should receive error tools/call response");

	/* Verify error response content */
	response = (mcp_tools_call_response_t *)mcp_transport_last_queued_msg.data;
	zassert_not_null(response, "Error response data should not be NULL");
	zassert_equal(response->request_id, 3004, "Error response should have correct request ID");
	zassert_true(strstr(response->result, "isError\": true") != NULL,
		     "Error response should indicate error");

	/* Test 5: Non-existent tool - now expects error response */
	printk("=== Test 5: Non-existent tool ===\n");
	int execution_count_before_nonexistent = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3005, "non_existent_tool", "{}");

	zassert_equal(tool_execution_count, execution_count_before_nonexistent,
		      "Non-existent tool should not execute");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Error response should be submitted for non-existent tool");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_CALL,
		      "Transport should receive tools/call error response");

	/* Verify error response */
	mcp_error_response_t *error_response =
		(mcp_error_response_t *)mcp_transport_last_queued_msg.data;
	zassert_not_null(error_response, "Error response data should not be NULL");
	zassert_equal(error_response->request_id, 3005,
		      "Error response should have correct request ID");

	/* Test 6: Tool execution from unregistered client - now expects error response */
	printk("=== Test 6: Unregistered client ===\n");
	int execution_count_before_unregistered = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_UNREGISTERED, 3006, "test_success_tool", "{}");

	zassert_equal(tool_execution_count, execution_count_before_unregistered,
		      "Unregistered client should not execute tools");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Error response should be submitted for unregistered client");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_CALL,
		      "Transport should receive tools/call error response");

	/* Test 7: Tool execution from non-initialized client - now expects error response */
	printk("=== Test 7: Non-initialized client ===\n");
	/* Register a new client but don't send initialized notification */
	send_initialize_request(CLIENT_ID_INITIALIZE_TEST, 3007);

	int execution_count_before_uninitialized = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_INITIALIZE_TEST, 3008, "test_success_tool", "{}");

	zassert_equal(tool_execution_count, execution_count_before_uninitialized,
		      "Non-initialized client should not execute tools");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Error response should be submitted for non-initialized client");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_CALL,
		      "Transport should receive tools/call error response");

	/* Test 8: Multiple tool executions */
	printk("=== Test 8: Multiple tool executions ===\n");
	int execution_count_before_multiple = tool_execution_count;

	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3009, "test_success_tool",
				"{\"test\":\"1\"}");
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3010, "test_success_tool",
				"{\"test\":\"2\"}");
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3011, "test_success_tool",
				"{\"test\":\"3\"}");

	zassert_equal(tool_execution_count, execution_count_before_multiple + 3,
		      "Multiple tool executions should work");

	/* Test 9: Long parameter string */
	printk("=== Test 9: Long parameter string ===\n");
	static char long_params[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN];
	memset(long_params, 'x', sizeof(long_params) - 1);
	long_params[sizeof(long_params) - 1] = '\0';

	int execution_count_before_long = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3012, "test_success_tool", long_params);

	zassert_equal(tool_execution_count, execution_count_before_long + 1,
		      "Long parameter string should work");
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Long parameter tool response should be submitted");

	/* Test 10: Slow tool execution */
	printk("=== Test 10: Slow tool execution ===\n");
	int execution_count_before_slow = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 3013, "test_slow_tool", "{}");

	zassert_equal(tool_execution_count, execution_count_before_slow + 1,
		      "Slow tool should complete execution");
	zassert_equal(mcp_transport_queue_call_count, 1, "Slow tool response should be submitted");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_CALL,
		      "Transport should receive slow tool response");

	/* Clean up */
	cleanup_test_tools();
	send_client_shutdown(CLIENT_ID_EDGE_CASE_TEST);
	send_client_shutdown(CLIENT_ID_INITIALIZE_TEST);

	zassert_equal(mcp_server_get_tool_count(), initial_tool_count,
		      "Tool count should return to initial value");

	printk("=== Comprehensive tools/call testing completed ===\n");
}

ZTEST(mcp_server_tests, test_tools_call_concurrent_clients)
{
	uint8_t initial_tool_count = mcp_server_get_tool_count();

	reset_tool_execution_tracking();
	reset_transport_mock();

	/* Register a test tool */
	mcp_tool_record_t concurrent_tool = {
		.metadata = {
				.name = "concurrent_test_tool",
				.input_schema = "{\"type\":\"object\"}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool for concurrent testing",
#endif
			},
		.callback = test_tool_success_callback};

	int ret = mcp_server_add_tool(&concurrent_tool);

	zassert_equal(ret, 0, "Concurrent test tool should register");

	/* Initialize multiple clients */
	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_1, REQ_ID_MULTI_CLIENT_1_INIT);
	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_2, REQ_ID_MULTI_CLIENT_2_INIT);

	printk("=== Testing concurrent tool execution from multiple clients ===\n");

	/* Execute tool from both clients */
	send_tools_call_request(CLIENT_ID_MULTI_CLIENT_1, 4001, "concurrent_test_tool",
				"{\"client\":\"1\"}");
	send_tools_call_request(CLIENT_ID_MULTI_CLIENT_2, 4002, "concurrent_test_tool",
				"{\"client\":\"2\"}");

	/* Both executions should succeed */
	zassert_equal(tool_execution_count, 2, "Tool should execute from both clients");

	/* Clean up */
	mcp_server_remove_tool("concurrent_test_tool");
	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_1);
	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_2);

	zassert_equal(mcp_server_get_tool_count(), initial_tool_count,
		      "Tool count should return to initial value");

	printk("=== Concurrent client testing completed ===\n");
}

ZTEST(mcp_server_tests, test_tools_call_edge_cases)
{
	uint8_t initial_tool_count = mcp_server_get_tool_count();

	reset_tool_execution_tracking();
	reset_transport_mock();

	/* Register a test tool */
	mcp_tool_record_t edge_case_tool = {
		.metadata = {
			.name = "edge_case_tool",
			.input_schema = "{\"type\":\"object\"}",
			},
		.callback = test_tool_success_callback
	};

	int ret = mcp_server_add_tool(&edge_case_tool);

	zassert_equal(ret, 0, "Edge case tool should register");

	initialize_client_fully(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_INITIALIZE);

	printk("=== Testing edge cases for tools/call ===\n");

	/* Test: Very long tool name (should not match) */
	char long_tool_name[CONFIG_MCP_TOOL_NAME_MAX_LEN + 10];

	memset(long_tool_name, 'a', sizeof(long_tool_name) - 1);

	long_tool_name[sizeof(long_tool_name) - 1] = '\0';

	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 5001, long_tool_name, "{}");
	zassert_equal(tool_execution_count, 0, "Tool with long name should not execute");

	/* Test: Empty tool name */
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 5002, "", "{}");
	zassert_equal(tool_execution_count, 0, "Tool with empty name should not execute");

	/* Test: Special characters in parameters */
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 5003, "edge_case_tool",
				"{\"special\":\"\\\"quotes\\\"\"}");
	zassert_equal(tool_execution_count, 1, "Tool with special characters should execute");

	/* Clean up */
	mcp_server_remove_tool("edge_case_tool");
	send_client_shutdown(CLIENT_ID_EDGE_CASE_TEST);

	zassert_equal(mcp_server_get_tool_count(), initial_tool_count,
		      "Tool count should return to initial value");

	printk("=== Edge case testing completed ===\n");
}

ZTEST(mcp_server_tests, test_memory_allocation)
{
	void *ptr1 = mcp_alloc(100);

	zassert_not_null(ptr1, "First allocation should succeed");

	void *ptr2 = mcp_alloc(200);

	zassert_not_null(ptr2, "Second allocation should succeed");

	zassert_not_equal(ptr1, ptr2, "Allocations should return different pointers");

	mcp_free(ptr1);
	mcp_free(ptr2);
	mcp_free(NULL);
}

ZTEST(mcp_server_tests, test_tools_list_response)
{
	int ret;
	uint8_t initial_tool_count = mcp_server_get_tool_count();

	reset_transport_mock();

	/* Unregistered client should now get error response */
	send_tools_list_request(CLIENT_ID_UNREGISTERED, REQ_ID_EDGE_CASE_UNREGISTERED);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tools/list should send error response for unregistered client");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_LIST,
		      "Should receive error response");

	mcp_tool_record_t test_tool1 = {
		.metadata = {
			.name = "test_tool_1",
			.input_schema = "{\"type\":\"object\",\"properties\":{}}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "First test tool for verification",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Test Tool One",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			.output_schema = "{\"type\":\"string\"}",
#endif
		},
		.callback = stub_tool_callback_1
	};

	mcp_tool_record_t test_tool2 = {
		.metadata = {
			.name = "test_tool_2",
			.input_schema = "{\"type\":\"array\"}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "Second test tool",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Test Tool Two",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			.output_schema = "{\"type\":\"number\"}",
#endif
		},
		.callback = stub_tool_callback_2
	};

	ret = mcp_server_add_tool(&test_tool1);
	zassert_equal(ret, 0, "Test tool 1 should register successfully");
	ret = mcp_server_add_tool(&test_tool2);
	zassert_equal(ret, 0, "Test tool 2 should register successfully");

	initialize_client_fully(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_INITIALIZE);

	reset_transport_mock();
	send_tools_list_request(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_TOOLS_LIST);

	zassert_equal(mcp_transport_queue_call_count, 1, "Tools/list should succeed");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_LIST,
		      "Response should be tools/list type");

	mcp_tools_list_response_t *response =
		(mcp_tools_list_response_t *)mcp_transport_last_queued_msg.data;

	zassert_not_null(response, "Response data should not be NULL");

	uint8_t expected_tool_count = initial_tool_count + 2;

	zassert_equal(response->tool_count, expected_tool_count,
		      "Response tool count should match registry");

	bool found_tool1 = false;
	bool found_tool2 = false;

	for (int i = 0; i < response->tool_count; i++) {
		if (strcmp(response->tools[i].name, "test_tool_1") == 0) {
			found_tool1 = true;
			zassert_str_equal(response->tools[i].input_schema,
					  "{\"type\":\"object\",\"properties\":{}}",
					  "Tool 1 input schema should match");
#ifdef CONFIG_MCP_TOOL_DESC
			zassert_str_equal(response->tools[i].description,
					  "First test tool for verification",
					  "Tool 1 description should match");
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			zassert_str_equal(response->tools[i].title, "Test Tool One",
					  "Tool 1 title should match");
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			zassert_str_equal(response->tools[i].output_schema, "{\"type\":\"string\"}",
					  "Tool 1 output schema should match");
#endif
		} else if (strcmp(response->tools[i].name, "test_tool_2") == 0) {
			found_tool2 = true;
			zassert_str_equal(response->tools[i].input_schema, "{\"type\":\"array\"}",
					  "Tool 2 input schema should match");
#ifdef CONFIG_MCP_TOOL_DESC
			zassert_str_equal(response->tools[i].description, "Second test tool",
					  "Tool 2 description should match");
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			zassert_str_equal(response->tools[i].title, "Test Tool Two",
					  "Tool 2 title should match");
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			zassert_str_equal(response->tools[i].output_schema, "{\"type\":\"number\"}",
					  "Tool 2 output schema should match");
#endif
		}

		zassert_true(strlen(response->tools[i].name) > 0, "Tool name should not be empty");
		zassert_true(strlen(response->tools[i].input_schema) > 0,
			     "Tool input schema should not be empty");
	}

	zassert_true(found_tool1, "Test tool 1 should be found in response");
	zassert_true(found_tool2, "Test tool 2 should be found in response");

	printk("Tool registry contains %d tools, verified tool content\n", response->tool_count);

	mcp_server_remove_tool("test_tool_1");
	mcp_server_remove_tool("test_tool_2");

	send_client_shutdown(CLIENT_ID_EDGE_CASE_TEST);
}

ZTEST(mcp_server_tests, test_initialize_request)
{
	reset_transport_mock();

	send_initialize_request(CLIENT_ID_INITIALIZE_TEST, REQ_ID_INITIALIZE_TEST);

	zassert_equal(mcp_transport_queue_call_count, 1, "Transport should be called once");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_INITIALIZE,
		      "Response type should be initialize");

	mcp_initialize_response_t *response =
		(mcp_initialize_response_t *)mcp_transport_last_queued_msg.data;

	zassert_not_null(response, "Response data should not be NULL");
	zassert_equal(response->request_id, REQ_ID_INITIALIZE_TEST,
		      "Response request ID should match");

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	zassert_true(response->capabilities & MCP_TOOLS,
		     "Tools capability should be set when CONFIG_MCP_TOOLS_CAPABILITY is enabled");
#endif

#ifdef CONFIG_MCP_SERVER_INFO_TITLE
	printk("Server info title feature is enabled\n");
#endif

#ifdef CONFIG_MCP_SERVER_INFO_INSTRUCTIONS
	printk("Server info instructions feature is enabled\n");
#endif

	send_client_shutdown(CLIENT_ID_INITIALIZE_TEST);
}

ZTEST(mcp_server_tests, test_tool_registration_valid)
{
	int ret;
	uint8_t initial_count = mcp_server_get_tool_count();

	mcp_tool_record_t valid_tool = {
		.metadata = {
			.name = "test_tool_valid",
			.input_schema = "{\"type\":\"object\"}",
#ifdef CONFIG_MCP_TOOL_DESC
			.description = "Test tool description",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
			.title = "Test Tool",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
			.output_schema = "{\"type\":\"object\"}",
#endif
		},
		.callback = stub_tool_callback_1
	};

	ret = mcp_server_add_tool(&valid_tool);
	zassert_equal(ret, 0, "Valid tool registration should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count + 1,
		      "Tool count should increase by 1");

	ret = mcp_server_remove_tool("test_tool_valid");
	zassert_equal(ret, 0, "Tool removal should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count,
		      "Tool count should return to initial value");
}

ZTEST(mcp_server_tests, test_tool_registration_duplicate)
{
	int ret;
	uint8_t initial_count = mcp_server_get_tool_count();

	mcp_tool_record_t tool1 = {
		.metadata = {
			.name = "duplicate_tool",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = stub_tool_callback_1
	};
	mcp_tool_record_t tool2 = {
		.metadata = {
			.name = "duplicate_tool",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = stub_tool_callback_2
	};

	ret = mcp_server_add_tool(&tool1);
	zassert_equal(ret, 0, "First tool registration should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count + 1, "Tool count should increase");

	ret = mcp_server_add_tool(&tool2);
	zassert_equal(ret, -EEXIST, "Duplicate tool registration should fail");
	zassert_equal(mcp_server_get_tool_count(), initial_count + 1,
		      "Tool count should not change on duplicate");

	ret = mcp_server_remove_tool("duplicate_tool");
	zassert_equal(ret, 0, "Tool cleanup should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count, "Tool count should be restored");
}

ZTEST(mcp_server_tests, test_tool_registration_edge_cases)
{
	int ret;
	uint8_t initial_count = mcp_server_get_tool_count();

	ret = mcp_server_add_tool(NULL);
	zassert_equal(ret, -EINVAL, "NULL tool_record should fail");

	mcp_tool_record_t empty_name_tool = {
		.metadata = {
			.name = "",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = stub_tool_callback_1
	};

	ret = mcp_server_add_tool(&empty_name_tool);
	zassert_equal(ret, -EINVAL, "Empty tool name should fail");

	mcp_tool_record_t null_callback_tool = {
		.metadata = {
			.name = "null_callback_tool",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = NULL
	};

	ret = mcp_server_add_tool(&null_callback_tool);
	zassert_equal(ret, -EINVAL, "NULL callback should fail");

	zassert_equal(mcp_server_get_tool_count(), initial_count,
		      "Tool count should not change after invalid attempts");

	mcp_tool_record_t registry_tools[] = {
		{.metadata = {.name = "registry_test_tool_1",
			      .input_schema = "{\"type\":\"object\"}"},
		 .callback = stub_tool_callback_3},
		{.metadata = {.name = "registry_test_tool_2",
			      .input_schema = "{\"type\":\"object\"}"},
		 .callback = stub_tool_callback_3},
		{.metadata = {.name = "registry_test_tool_3",
			      .input_schema = "{\"type\":\"object\"}"},
		 .callback = stub_tool_callback_3},
		{.metadata = {.name = "registry_test_tool_4",
			      .input_schema = "{\"type\":\"object\"}"},
		 .callback = stub_tool_callback_3}
	};

	for (int i = 0; i < ARRAY_SIZE(registry_tools); i++) {
		ret = mcp_server_add_tool(&registry_tools[i]);
		zassert_equal(ret, 0, "Tool %d should register successfully", i + 1);
	}

	zassert_equal(mcp_server_get_tool_count(), CONFIG_MCP_MAX_TOOLS,
		      "Registry should be at maximum capacity");

	mcp_tool_record_t overflow_tool = {
		.metadata = {
			.name = "registry_overflow_tool",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = stub_tool_callback_3
	};

	ret = mcp_server_add_tool(&overflow_tool);
	zassert_equal(ret, -ENOSPC, "Registry overflow should fail");
	zassert_equal(mcp_server_get_tool_count(), CONFIG_MCP_MAX_TOOLS,
		      "Tool count should not change when registry is full");

	for (int i = 0; i < ARRAY_SIZE(registry_tools); i++) {
		mcp_server_remove_tool(registry_tools[i].metadata.name);
	}

	zassert_equal(mcp_server_get_tool_count(), initial_count,
		      "Tool count should return to initial value");
}

ZTEST(mcp_server_tests, test_tool_removal)
{
	int ret;
	uint8_t initial_count = mcp_server_get_tool_count();

	mcp_tool_record_t test_tool = {
		.metadata = {
			.name = "removal_test_tool",
			.input_schema = "{\"type\":\"object\"}",
		},
		.callback = stub_tool_callback_1
	};

	ret = mcp_server_add_tool(&test_tool);
	zassert_equal(ret, 0, "Tool addition should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count + 1, "Tool count should increase");

	ret = mcp_server_remove_tool("removal_test_tool");
	zassert_equal(ret, 0, "Tool removal should succeed");
	zassert_equal(mcp_server_get_tool_count(), initial_count, "Tool count should decrease");

	ret = mcp_server_remove_tool("removal_test_tool");
	zassert_equal(ret, -ENOENT, "Removing non-existent tool should fail");

	ret = mcp_server_remove_tool(NULL);
	zassert_equal(ret, -EINVAL, "NULL tool name should fail");

	ret = mcp_server_remove_tool("");
	zassert_equal(ret, -EINVAL, "Empty tool name should fail");

	ret = mcp_server_remove_tool("never_existed_tool");
	zassert_equal(ret, -ENOENT, "Non-existent tool should fail");
}

ZTEST(mcp_server_tests, test_client_lifecycle)
{
	reset_transport_mock();

	send_initialize_request(CLIENT_ID_LIFECYCLE_TEST, REQ_ID_LIFECYCLE_INITIALIZE);
	zassert_equal(mcp_transport_queue_call_count, 1, "Initialize response should be sent");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_INITIALIZE,
		      "Response should be initialize type");

	reset_transport_mock();
	send_tools_list_request(CLIENT_ID_LIFECYCLE_TEST, REQ_ID_LIFECYCLE_TOOLS_INIT);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tools/list should send error response before client is initialized");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_LIST,
		      "Should receive error response");

	send_initialized_notification(CLIENT_ID_LIFECYCLE_TEST);

	reset_transport_mock();
	send_tools_list_request(CLIENT_ID_LIFECYCLE_TEST, REQ_ID_LIFECYCLE_TOOLS_READY);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tools/list should succeed after initialization");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_RESPONSE_TOOLS_LIST,
		      "Response should be tools/list type");

	send_client_shutdown(CLIENT_ID_LIFECYCLE_TEST);
}

ZTEST(mcp_server_tests, test_client_shutdown)
{
	reset_transport_mock();

	initialize_client_fully(CLIENT_ID_SHUTDOWN_TEST, REQ_ID_SHUTDOWN_INITIALIZE);
	zassert_equal(mcp_transport_queue_call_count, 1, "Client initialization should succeed");

	reset_transport_mock();
	send_tools_list_request(CLIENT_ID_SHUTDOWN_TEST, REQ_ID_SHUTDOWN_TOOLS_ACTIVE);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tools/list should work for active client");

	reset_transport_mock();
	send_client_shutdown(CLIENT_ID_SHUTDOWN_TEST);
	zassert_equal(mcp_transport_queue_call_count, 0, "No response expected for shutdown");

	reset_transport_mock();
	send_tools_list_request(CLIENT_ID_SHUTDOWN_TEST, REQ_ID_SHUTDOWN_TOOLS_DEAD);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Tools/list should send error response for shutdown client");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_TOOLS_LIST,
		      "Should receive error response");

	reset_transport_mock();
	send_client_shutdown(CLIENT_ID_UNREGISTERED);
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Shutdown of unregistered client should be handled gracefully");
}

ZTEST(mcp_server_tests, test_invalid_states)
{
	reset_transport_mock();

	initialize_client_fully(CLIENT_ID_INVALID_STATE_TEST, REQ_ID_INVALID_INITIALIZE);
	zassert_equal(mcp_transport_queue_call_count, 1, "Normal initialization should succeed");

	reset_transport_mock();
	send_initialize_request(CLIENT_ID_INVALID_STATE_TEST, REQ_ID_INVALID_REINITIALIZE);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "Re-initialization should send error response");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_INITIALIZE,
		      "Should receive initialize error response");

	reset_transport_mock();
	send_initialized_notification(CLIENT_ID_UNREGISTERED);
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Notification for unregistered client should be rejected");

	reset_transport_mock();
	send_initialized_notification(CLIENT_ID_INVALID_STATE_TEST);
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Duplicate initialized notification should be rejected");

	send_client_shutdown(CLIENT_ID_INVALID_STATE_TEST);
}

ZTEST(mcp_server_tests, test_multiple_client_lifecycle)
{
	reset_transport_mock();

	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_1, REQ_ID_MULTI_CLIENT_1_INIT);
	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_2, REQ_ID_MULTI_CLIENT_2_INIT);
	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_3, REQ_ID_MULTI_CLIENT_3_INIT);

	zassert_equal(mcp_transport_queue_call_count, 3,
		      "All 3 clients should initialize successfully");

	reset_transport_mock();
	send_initialize_request(CLIENT_ID_MULTI_CLIENT_4, REQ_ID_MULTI_CLIENT_4_INIT_1);
	zassert_equal(mcp_transport_queue_call_count, 1,
		      "4th client should send error response when registry is full");
	zassert_equal(mcp_transport_last_queued_msg.type, MCP_MSG_ERROR_INITIALIZE,
		      "Should receive initialize error response");

	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_1);

	reset_transport_mock();
	initialize_client_fully(CLIENT_ID_MULTI_CLIENT_4, REQ_ID_MULTI_CLIENT_4_INIT_2);
	zassert_equal(mcp_transport_queue_call_count, 1, "4th client should succeed after cleanup");

	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_2);
	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_3);
	send_client_shutdown(CLIENT_ID_MULTI_CLIENT_4);
}

ZTEST(mcp_server_tests, test_unknown_message_type)
{
	int ret;
	mcp_request_queue_msg_t msg;
	char *test_data;

	reset_transport_mock();

	test_data = (char *)mcp_alloc(32);
	zassert_not_null(test_data, "Test data allocation should succeed");
	strcpy(test_data, "invalid_message_data");

	msg.type = MCP_MSG_INVALID_TYPE;
	msg.data = test_data;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Invalid message queueing should succeed");
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "No response should be sent for unknown message type");
}

ZTEST(mcp_server_tests, test_server_double_init)
{
	int ret;

	ret = mcp_server_init();
	zassert_equal(ret, 0, "Second server init should succeed or handle gracefully");
}

ZTEST(mcp_server_tests, test_null_data_request)
{
	int ret;
	mcp_request_queue_msg_t msg;

	reset_transport_mock();

	msg.type = MCP_MSG_REQUEST_INITIALIZE;
	msg.data = NULL;

	ret = k_msgq_put(&mcp_request_queue, &msg, K_NO_WAIT);
	zassert_equal(ret, 0, "NULL data message queueing should succeed");
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "No response should be sent for NULL data pointer");
}

ZTEST(mcp_server_tests, test_tools_call)
{
	int ret;
	uint8_t initial_tool_count = mcp_server_get_tool_count();

	tool_execution_count = 0;

	reset_transport_mock();

	mcp_tool_record_t execution_tool = {
		.metadata = {
				.name = "execution_test_tool",
				.input_schema = "{\"type\":\"object\",\"properties\":"
						"{\"param1\":{\"type\":\"string\"}}}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool for testing execution",
#endif
#ifdef CONFIG_MCP_TOOL_TITLE
				.title = "Execution Test Tool",
#endif
#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
				.output_schema = "{\"type\":\"string\"}",
#endif
			},
		.callback = test_tool_success_callback};

	ret = mcp_server_add_tool(&execution_tool);
	zassert_equal(ret, 0, "Execution test tool should register successfully");
	zassert_equal(mcp_server_get_tool_count(), initial_tool_count + 1,
		      "Tool count should increase");

	initialize_client_fully(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_INITIALIZE);

	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_TOOLS_LIST,
				"execution_test_tool", "{\"param1\":\"test_value\"}");

	zassert_equal(tool_execution_count, 1, "Tool callback should have been executed once");

	int previous_count = tool_execution_count;

	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_TOOLS_LIST + 1,
				"non_existent_tool", "{}");

	zassert_equal(tool_execution_count, previous_count,
		      "Tool callback should not be executed for non-existent tool");

	previous_count = tool_execution_count;
	reset_transport_mock();
	send_tools_call_request(CLIENT_ID_UNREGISTERED, REQ_ID_EDGE_CASE_UNREGISTERED,
				"execution_test_tool", "{}");

	zassert_equal(tool_execution_count, previous_count,
		      "Tool callback should not be executed for unregistered client");

	mcp_server_remove_tool("execution_test_tool");
	zassert_equal(mcp_server_get_tool_count(), initial_tool_count,
		      "Tool count should return to initial value");

	send_client_shutdown(CLIENT_ID_EDGE_CASE_TEST);

	printk("Tool execution test completed successfully\n");
}

ZTEST(mcp_server_tests, test_invalid_execution_tokens)
{
	int ret;
	mcp_app_message_t app_msg;
	char response_data[] = "{"
			       "\"content\": ["
			       "{"
			       "\"type\": \"text\","
			       "\"text\": \"This should not be accepted\""
			       "}"
			       "],"
			       "\"isError\": false"
			       "}";

	app_msg.type = MCP_USR_TOOL_RESPONSE;
	app_msg.data = response_data;
	app_msg.length = strlen(response_data);

	printk("=== Testing invalid execution tokens ===\n");

	/* Test 1: Zero execution token (invalid) */
	printk("=== Test 1: Zero execution token ===\n");
	ret = mcp_server_submit_app_message(&app_msg, 0);
	zassert_equal(ret, -EINVAL, "Zero execution token should be rejected with -EINVAL");

	/* Test 2: Non-existent execution token */
	printk("=== Test 2: Non-existent execution token ===\n");

	uint32_t fake_token = 99999;

	ret = mcp_server_submit_app_message(&app_msg, fake_token);
	zassert_equal(ret, -ENOENT, "Non-existent execution token should be rejected with -ENOENT");

	/* Test 3: Create a valid token, use it, then try to use it again */
	printk("=== Test 3: Reusing completed execution token ===\n");

	uint8_t initial_tool_count = mcp_server_get_tool_count();

	/* Register a test tool for this test */
	mcp_tool_record_t token_test_tool = {
		.metadata = {
				.name = "token_test_tool",
				.input_schema = "{\"type\":\"object\"}",
#ifdef CONFIG_MCP_TOOL_DESC
				.description = "Tool for testing execution tokens",
#endif
			},
		.callback = test_tool_success_callback};

	ret = mcp_server_add_tool(&token_test_tool);
	zassert_equal(ret, 0, "Test tool should register successfully");

	/* Initialize a client and execute a tool to get a valid token */
	initialize_client_fully(CLIENT_ID_EDGE_CASE_TEST, REQ_ID_EDGE_CASE_INITIALIZE);

	/* Reset execution tracking */
	reset_tool_execution_tracking();

	/* Execute the tool - this will create and then destroy an execution token */
	send_tools_call_request(CLIENT_ID_EDGE_CASE_TEST, 4500, "token_test_tool", "{}");
	zassert_equal(tool_execution_count, 1, "Tool should have executed once");
	uint32_t used_token = last_execution_token;

	zassert_not_equal(used_token, 0, "Should have captured the execution token");

	/* Now try to use the same token again - it should be rejected */
	printk("=== Test 3a: Attempting to reuse token %u ===\n", used_token);
	ret = mcp_server_submit_app_message(&app_msg, used_token);
	zassert_equal(ret, -ENOENT, "Completed execution token should be rejected with -ENOENT");

	/* Test 4: NULL app_msg */
	printk("=== Test 4: NULL app_msg ===\n");
	ret = mcp_server_submit_app_message(NULL, 1234);
	zassert_equal(ret, -EINVAL, "NULL app_msg should be rejected with -EINVAL");

	/* Test 5: app_msg with NULL data */
	printk("=== Test 5: app_msg with NULL data ===\n");
	mcp_app_message_t null_data_msg = {
		.type = MCP_USR_TOOL_RESPONSE, .data = NULL, .length = 10};
	ret = mcp_server_submit_app_message(&null_data_msg, 1234);
	zassert_equal(ret, -EINVAL, "app_msg with NULL data should be rejected with -EINVAL");

	/* Clean up */
	mcp_server_remove_tool("token_test_tool");
	send_client_shutdown(CLIENT_ID_EDGE_CASE_TEST);

	zassert_equal(mcp_server_get_tool_count(), initial_tool_count,
		      "Tool count should return to initial value");

	printk("=== Invalid execution token testing completed ===\n");
}

static void *mcp_server_tests_setup(void)
{
	int ret;

	ret = mcp_server_init();
	zassert_equal(ret, 0, "Server initialization should succeed");

	ret = mcp_server_start();
	zassert_equal(ret, 0, "Server start should succeed");

	k_msleep(100);

	return NULL;
}

static void mcp_server_tests_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_transport_mock();
}

ZTEST_SUITE(mcp_server_tests, NULL, mcp_server_tests_setup, mcp_server_tests_before, NULL, NULL);
