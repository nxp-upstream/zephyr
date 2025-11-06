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
extern mcp_response_queue_msg_t mcp_transport_last_queued_msg;

static int tool_execution_count;

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

static int stub_tool_callback_1(const char *params, uint32_t execution_token)
{
	return 0;
}

static int stub_tool_callback_2(const char *params, uint32_t execution_token)
{
	return 0;
}

static int stub_tool_callback_3(const char *params, uint32_t execution_token)
{
	return 0;
}

static void reset_transport_mock(void)
{
	mcp_transport_queue_call_count = 0;
	memset(&mcp_transport_last_queued_msg, 0, sizeof(mcp_transport_last_queued_msg));
}

static int test_execution_tool_callback(const char *params, uint32_t execution_token)
{
	tool_execution_count++;

	printk("Tool execution callback executed! Count: %d, Token: %u, Arguments: %s\n",
	       tool_execution_count, execution_token, params ? params : "(null)");

	return 0;
}

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

	k_msleep(100);
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

	send_tools_list_request(CLIENT_ID_UNREGISTERED, REQ_ID_EDGE_CASE_UNREGISTERED);
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Tools/list should be rejected for unregistered client");

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
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Tools/list should be rejected before client is initialized");

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
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "Tools/list should be rejected for shutdown client");

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
	zassert_equal(mcp_transport_queue_call_count, 0, "Re-initialization should be rejected");

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
	zassert_equal(mcp_transport_queue_call_count, 0,
		      "4th client should be rejected when registry is full");

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

	k_msleep(100);

	zassert_equal(mcp_transport_queue_call_count, 0,
		      "No response should be sent for unknown message type");
}

ZTEST(mcp_server_tests, test_server_double_init)
{
	int ret;

	ret = mcp_server_init();
	zassert_equal(ret, 0, "Second server init should succeed or handle gracefully");
}

ZTEST(mcp_server_tests, test_message_worker)
{
	int ret;
	struct mcp_message_msg test_msg;

	memset(&test_msg, 0, sizeof(test_msg));

	ret = k_msgq_put(&mcp_message_queue, &test_msg, K_NO_WAIT);
	zassert_equal(ret, 0, "Message queueing should succeed");

	k_msleep(100);
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

	k_msleep(100);

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
		.callback = test_execution_tool_callback
	};

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
