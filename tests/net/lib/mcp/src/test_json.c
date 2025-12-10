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
#include "mcp_json.h"

/* ============================================================================
 * JSON Parsing Tests - Initialize Request
 * ============================================================================
 */

ZTEST(mcp_json_parse, test_parse_initialize_request_valid)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
		      "\"params\":{\"protocolVersion\":\"2024-11-05\","
		      "\"clientInfo\":{\"name\":\"test-client\",\"version\":\"1.0.0\"}}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 123, &type, &data);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(type, MCP_MSG_REQUEST_INITIALIZE, "Should be initialize request");
	zassert_not_null(data, "Data should not be NULL");

	mcp_initialize_request_t *req = (mcp_initialize_request_t *)data;
	zassert_equal(req->request_id, 1, "Request ID should be 1");
	zassert_equal(req->client_id, 123, "Client ID should be 123");

	mcp_free(data);
}

ZTEST(mcp_json_parse, test_parse_initialize_request_invalid_jsonrpc_version)
{
	char json[] = "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"initialize\","
		      "\"params\":{\"protocolVersion\":\"2024-11-05\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 123, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with invalid JSON-RPC version");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_initialize_request_missing_method)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":1,"
		      "\"params\":{\"protocolVersion\":\"2024-11-05\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 123, &type, &data);

	zassert_true(ret < 0, "Should fail with missing method");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_initialize_request_wrong_method)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"wrong_method\","
		      "\"params\":{\"protocolVersion\":\"2024-11-05\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 123, &type, &data);

	zassert_equal(ret, -ENOTSUP, "Should fail with unknown method");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_initialize_request_invalid_client_id)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
		      "\"params\":{\"protocolVersion\":\"2024-11-05\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 0, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with invalid client_id");
	zassert_is_null(data, "Data should be NULL on error");
}

/* ============================================================================
 * JSON Parsing Tests - Tools List Request
 * ============================================================================
 */

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
ZTEST(mcp_json_parse, test_parse_tools_list_request_valid)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 456, &type, &data);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(type, MCP_MSG_REQUEST_TOOLS_LIST, "Should be tools/list request");
	zassert_not_null(data, "Data should not be NULL");

	mcp_tools_list_request_t *req = (mcp_tools_list_request_t *)data;
	zassert_equal(req->request_id, 2, "Request ID should be 2");
	zassert_equal(req->client_id, 456, "Client ID should be 456");

	mcp_free(data);
}

ZTEST(mcp_json_parse, test_parse_tools_call_request_valid)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
		      "\"params\":{\"name\":\"test_tool\","
		      "\"arguments\":\"{\\\"param1\\\":\\\"value1\\\"}\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 789, &type, &data);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(type, MCP_MSG_REQUEST_TOOLS_CALL, "Should be tools/call request");
	zassert_not_null(data, "Data should not be NULL");

	mcp_tools_call_request_t *req = (mcp_tools_call_request_t *)data;
	zassert_equal(req->request_id, 3, "Request ID should be 3");
	zassert_equal(req->client_id, 789, "Client ID should be 789");
	zassert_mem_equal(req->name, "test_tool", strlen("test_tool"), "Tool name should match");

	mcp_free(data);
}

ZTEST(mcp_json_parse, test_parse_tools_call_request_missing_name)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
		      "\"params\":{\"arguments\":\"{\\\"param1\\\":\\\"value1\\\"}\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 789, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with missing tool name");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_tools_call_request_empty_arguments)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
		      "\"params\":{\"name\":\"test_tool\"}}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 789, &type, &data);

	zassert_equal(ret, 0, "Parse should succeed with empty arguments");
	zassert_not_null(data, "Data should not be NULL");

	mcp_tools_call_request_t *req = (mcp_tools_call_request_t *)data;
	zassert_equal(req->arguments[0], '\0', "Arguments should be empty string");

	mcp_free(data);
}
#endif /* CONFIG_MCP_TOOLS_CAPABILITY */

/* ============================================================================
 * JSON Parsing Tests - Notifications
 * ============================================================================
 */

ZTEST(mcp_json_parse, test_parse_initialized_notification_valid)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 999, &type, &data);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(type, MCP_MSG_NOTIFICATION, "Should be notification");
	zassert_not_null(data, "Data should not be NULL");

	mcp_client_notification_t *notif = (mcp_client_notification_t *)data;
	zassert_equal(notif->client_id, 999, "Client ID should be 999");
	zassert_equal(notif->method, MCP_NOTIF_INITIALIZED, "Should be initialized notification");

	mcp_free(data);
}

ZTEST(mcp_json_parse, test_parse_notification_unknown_method)
{
	char json[] = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/unknown\"}";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 999, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with unknown notification method");
	zassert_is_null(data, "Data should be NULL on error");
}

/* ============================================================================
 * JSON Parsing Tests - Invalid Input
 * ============================================================================
 */

ZTEST(mcp_json_parse, test_parse_null_json)
{
	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(NULL, 100, 123, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with NULL JSON");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_zero_length)
{
	char json[] = "{\"jsonrpc\":\"2.0\"}";
	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, 0, 123, &type, &data);

	zassert_equal(ret, -EINVAL, "Should fail with zero length");
	zassert_is_null(data, "Data should be NULL on error");
}

ZTEST(mcp_json_parse, test_parse_malformed_json)
{

	char json[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"";

	mcp_queue_msg_type_t type;
	void *data = NULL;
	int ret;

	ret = mcp_json_parse_request(json, strlen(json), 123, &type, &data);

	zassert_true(ret < 0, "Should fail with malformed JSON");
	zassert_is_null(data, "Data should be NULL on error");
}

/* ============================================================================
 * JSON Serialization Tests - Initialize Response
 * ============================================================================
 */

ZTEST(mcp_json_serialize, test_serialize_initialize_response_basic)
{
	mcp_initialize_response_t resp = {
		.request_id = 1,
		.capabilities = MCP_TOOLS,
	};
	char buffer[512];
	int ret;

	ret = mcp_json_serialize_initialize_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");

	/* Verify JSON structure per MCP spec */
	zassert_not_null(strstr(buffer, "\"jsonrpc\":\"2.0\""), "Should have jsonrpc field");
	zassert_not_null(strstr(buffer, "\"id\":1"), "Should have id field");
	zassert_not_null(strstr(buffer, "\"protocolVersion\":\"2024-11-05\""),
			 "Should have protocol version");
	zassert_not_null(strstr(buffer, "\"tools\":{}"), "Should have tools capability as object");
	zassert_not_null(strstr(buffer, "\"serverInfo\""), "Should have serverInfo");
}

ZTEST(mcp_json_serialize, test_serialize_initialize_response_no_tools)
{
	mcp_initialize_response_t resp = {
		.request_id = 2,
		.capabilities = 0,  /* No tools */
	};
	char buffer[512];
	int ret;

	ret = mcp_json_serialize_initialize_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");
	/* When tools not supported, it should be omitted */
	zassert_is_null(strstr(buffer, "\"tools\""), "Should NOT have tools field when not supported");
}

ZTEST(mcp_json_serialize, test_serialize_initialize_response_buffer_too_small)
{
	mcp_initialize_response_t resp = {
		.request_id = 1,
		.capabilities = MCP_TOOLS,
	};
	char buffer[10];
	int ret;

	ret = mcp_json_serialize_initialize_response(&resp, buffer, sizeof(buffer));

	zassert_equal(ret, -ENOMEM, "Should fail with buffer too small");
}

/* ============================================================================
 * JSON Serialization Tests - Tools List Response
 * ============================================================================
 */

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
ZTEST(mcp_json_serialize, test_serialize_tools_list_response_empty)
{
	mcp_tools_list_response_t resp = {
		.request_id = 3,
		.tool_count = 0,
	};
	char buffer[512];
	int ret;

	ret = mcp_json_serialize_tools_list_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");
	zassert_not_null(strstr(buffer, "\"jsonrpc\":\"2.0\""), "Should have jsonrpc field");
	zassert_not_null(strstr(buffer, "\"id\":3"), "Should have id field");
	zassert_not_null(strstr(buffer, "\"tools\":[]"), "Should have empty tools array");
}

ZTEST(mcp_json_serialize, test_serialize_tools_list_response_single_tool)
{
	mcp_tools_list_response_t resp = {
		.request_id = 4,
		.tool_count = 1,
	};

	strncpy(resp.tools[0].name, "test_tool", CONFIG_MCP_TOOL_NAME_MAX_LEN);
	strncpy(resp.tools[0].input_schema, "{\"type\":\"object\"}",
		CONFIG_MCP_TOOL_SCHEMA_MAX_LEN);

	char buffer[1024];
	int ret;

	ret = mcp_json_serialize_tools_list_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");
	zassert_not_null(strstr(buffer, "\"name\":\"test_tool\""), "Should have tool name");
	zassert_not_null(strstr(buffer, "\"inputSchema\":{\"type\":\"object\"}"),
			 "Should have input schema");
}

ZTEST(mcp_json_serialize, test_serialize_tools_list_response_multiple_tools)
{
	mcp_tools_list_response_t resp = {
		.request_id = 5,
		.tool_count = 2,
	};

	strncpy(resp.tools[0].name, "tool1", CONFIG_MCP_TOOL_NAME_MAX_LEN);
	strncpy(resp.tools[0].input_schema, "{\"type\":\"object\"}",
		CONFIG_MCP_TOOL_SCHEMA_MAX_LEN);

	strncpy(resp.tools[1].name, "tool2", CONFIG_MCP_TOOL_NAME_MAX_LEN);
	strncpy(resp.tools[1].input_schema, "{\"type\":\"string\"}",
		CONFIG_MCP_TOOL_SCHEMA_MAX_LEN);

	char buffer[2048];
	int ret;

	ret = mcp_json_serialize_tools_list_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");
	zassert_not_null(strstr(buffer, "\"name\":\"tool1\""), "Should have tool1");
	zassert_not_null(strstr(buffer, "\"name\":\"tool2\""), "Should have tool2");
}

ZTEST(mcp_json_serialize, test_serialize_tools_call_response_valid)
{
	mcp_tools_call_response_t resp = {
		.request_id = 3,
		.length = 18,
	};
	strcpy(resp.result, "\"Test result text\"");
	char buffer[512];
	int ret;

	ret = mcp_json_serialize_tools_call_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");

	/* Result should contain content array per MCP spec */
	zassert_not_null(strstr(buffer, "\"content\":["), "Should have content array");
	zassert_not_null(strstr(buffer, "\"type\":\"text\""), "Should have text type");
	zassert_not_null(strstr(buffer, "\"text\":"), "Should have text field");
}
#endif /* CONFIG_MCP_TOOLS_CAPABILITY */

/* ============================================================================
 * JSON Serialization Tests - Error Response
 * ============================================================================
 */

ZTEST(mcp_json_serialize, test_serialize_error_response_valid)
{
	mcp_error_response_t resp = {
		.request_id = 7,
		.error_code = MCP_ERROR_INVALID_REQUEST,
	};
	strncpy(resp.error_message, "Invalid request", sizeof(resp.error_message));

	char buffer[512];
	int ret;

	ret = mcp_json_serialize_error_response(&resp, buffer, sizeof(buffer));

	zassert_true(ret > 0, "Serialization should succeed");
	zassert_not_null(strstr(buffer, "\"jsonrpc\":\"2.0\""), "Should have jsonrpc field");
	zassert_not_null(strstr(buffer, "\"id\":7"), "Should have id field");
	zassert_not_null(strstr(buffer, "\"error\""), "Should have error field");
	zassert_not_null(strstr(buffer, "\"code\":-32600"), "Should have error code");
	zassert_not_null(strstr(buffer, "\"message\":\"Invalid request\""),
			 "Should have error message");
}

ZTEST(mcp_json_serialize, test_serialize_error_response_all_error_codes)
{
	struct {
		int32_t code;
		const char *name;
	} error_codes[] = {
		{MCP_ERROR_PARSE_ERROR, "Parse error"},
		{MCP_ERROR_INVALID_REQUEST, "Invalid request"},
		{MCP_ERROR_METHOD_NOT_FOUND, "Method not found"},
		{MCP_ERROR_INVALID_PARAMS, "Invalid params"},
		{MCP_ERROR_INTERNAL_ERROR, "Internal error"},
		{MCP_ERROR_SERVER_ERROR, "Server error"},
	};

	for (int i = 0; i < ARRAY_SIZE(error_codes); i++) {
		mcp_error_response_t resp = {
			.request_id = 100 + i,
			.error_code = error_codes[i].code,
		};
		strncpy(resp.error_message, error_codes[i].name,
			sizeof(resp.error_message));

		char buffer[512];
		int ret;

		ret = mcp_json_serialize_error_response(&resp, buffer, sizeof(buffer));

		zassert_true(ret > 0, "Serialization should succeed for error code %d",
			     error_codes[i].code);
	}
}

/* ============================================================================
 * Test Suites
 * ============================================================================
 */
ZTEST_SUITE(mcp_json_parse, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(mcp_json_serialize, NULL, NULL, NULL, NULL, NULL);
