/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>
#include <string.h>
#include <stdlib.h>
#include "mcp_json.h"
#include "mcp_common.h"

LOG_MODULE_REGISTER(mcp_json, CONFIG_MCP_LOG_LEVEL);

#define MAX_METHOD_NAME_LEN 64
#define MAX_TOOL_NAME_LEN 128
/* =============================================================================
 * JSON-RPC 2.0 Base Structures
 * =============================================================================
 */

/* Base JSON-RPC request structure */
struct jsonrpc_request {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
};

/* JSON-RPC notification (no id) */
struct jsonrpc_notification {
	const char *jsonrpc;
	const char *method;
};

static const struct json_obj_descr jsonrpc_notification_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, method, JSON_TOK_STRING),
};

/* =============================================================================
 * Initialize Request/Response
 * =============================================================================
 */

struct client_info {
	const char *name;
	const char *version;
	/* Optional fields - check for NULL after parsing */
	const char *title;
	const char *description;
	const char *websiteUrl;
};

static const struct json_obj_descr client_info_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct client_info, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct client_info, version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct client_info, title, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct client_info, description, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct client_info, websiteUrl, JSON_TOK_STRING),
};

struct initialize_params {
	const char *protocolVersion;
	struct client_info clientInfo;
	/* capabilities is optional and we don't parse its contents */
};

static const struct json_obj_descr initialize_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct initialize_params, protocolVersion, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct initialize_params, clientInfo, client_info_descr),
	/* capabilities object is optional and ignored */
};

struct initialize_request_json {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
	struct initialize_params params;
};

static const struct json_obj_descr initialize_request_json_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct initialize_request_json, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct initialize_request_json, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct initialize_request_json, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct initialize_request_json, params, initialize_params_descr),
};

/* =============================================================================
 * Ping Request/Response
 * =============================================================================
 */

/* Ping has no parameters, uses base jsonrpc_request */

/* =============================================================================
 * Tools - List Request/Response
 * =============================================================================
 */

struct tools_list_params {
	const char *cursor;  /* Optional pagination cursor */
};

static const struct json_obj_descr tools_list_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_list_params, cursor, JSON_TOK_STRING),
};

struct tools_list_request_json {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
	struct tools_list_params params;
};

static const struct json_obj_descr tools_list_request_json_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_list_request_json, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct tools_list_request_json, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct tools_list_request_json, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct tools_list_request_json, params, tools_list_params_descr),
};

/* =============================================================================
 * Tools - Call Request/Response
 * =============================================================================
 */

struct tools_call_params {
	const char *name;
	/* arguments is extracted manually from raw JSON */
};

static const struct json_obj_descr tools_call_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_call_params, name, JSON_TOK_STRING),
};

struct tools_call_request_json {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
	struct tools_call_params params;
};

static const struct json_obj_descr tools_call_request_json_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_json, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_json, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_json, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct tools_call_request_json, params, tools_call_params_descr),
};

/* =============================================================================
 * Notifications
 * =============================================================================
 */

/* notifications/initialized - no params */

/* notifications/cancelled */
struct cancelled_params {
	const char *requestId;
	const char *reason;  /* Optional */
};

static const struct json_obj_descr cancelled_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct cancelled_params, requestId, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cancelled_params, reason, JSON_TOK_STRING),
};

struct cancelled_notification_json {
	const char *jsonrpc;
	const char *method;
	struct cancelled_params params;
};

static const struct json_obj_descr cancelled_notification_json_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct cancelled_notification_json, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cancelled_notification_json, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct cancelled_notification_json, params, cancelled_params_descr),
};

/* notifications/progress */
struct progress_params {
	const char *progressToken;
	uint32_t progress;
	uint32_t total;  /* Optional */
	const char *message;  /* Optional */
};

static const struct json_obj_descr progress_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct progress_params, progressToken, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct progress_params, progress, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct progress_params, total, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct progress_params, message, JSON_TOK_STRING),
};

struct progress_notification_json {
	const char *jsonrpc;
	const char *method;
	struct progress_params params;
};

static const struct json_obj_descr progress_notification_json_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct progress_notification_json, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct progress_notification_json, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct progress_notification_json, params, progress_params_descr),
};

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

static char *create_json_copy(const char *json, size_t length)
{
	char *copy;

	if (!json || length == 0) {
		return NULL;
	}

	copy = (char *)mcp_alloc(length + 1);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, json, length);
	copy[length] = '\0';

	return copy;
}

static bool is_valid_jsonrpc_version(const char *version)
{
	if (!version) {
		return false;
	}
	return (strcmp(version, "2.0") == 0);
}

/**
 * @brief Extract arguments object from JSON manually
 *
 * The Zephyr JSON parser doesn't easily handle arbitrary JSON objects,
 * so we extract the "arguments" field as a raw JSON string.
 */
static int extract_arguments_field(const char *json, size_t length,
					char *output_buf, size_t buf_size)
{
	const char *args_start = strstr(json, "\"arguments\"");
	if (!args_start) {
		/* Arguments are optional */
		output_buf[0] = '{';
		output_buf[1] = '}';
		output_buf[2] = '\0';
		return 0;
	}

	/* Find the colon after "arguments" */
	const char *colon = strchr(args_start, ':');
	if (!colon) {
		return -EINVAL;
	}

	/* Skip whitespace after colon */
	const char *value_start = colon + 1;
	while (*value_start && (*value_start == ' ' || *value_start == '\t' ||
		   *value_start == '\n' || *value_start == '\r')) {
		value_start++;
	}

	if (*value_start != '{') {
		LOG_ERR("Expected '{' at start of arguments object");
		return -EINVAL;
	}

	/* Find matching closing brace */
	const char *value_end = value_start + 1;
	int brace_count = 1;
	bool in_string = false;
	bool escaped = false;

	while (*value_end && brace_count > 0) {
		if (escaped) {
			escaped = false;
		} else if (*value_end == '\\') {
			escaped = true;
		} else if (*value_end == '"') {
			in_string = !in_string;
		} else if (!in_string) {
			if (*value_end == '{') {
				brace_count++;
			} else if (*value_end == '}') {
				brace_count--;
			}
		}
		if (brace_count > 0) {
			value_end++;
		}
	}

	if (brace_count != 0) {
		LOG_ERR("Unmatched braces in arguments object");
		return -EINVAL;
	}

	/* Include the closing brace */
	value_end++;

	size_t arg_len = value_end - value_start;
	if (arg_len >= buf_size) {
		LOG_ERR("Arguments too large: %zu bytes (max %zu)", arg_len, buf_size - 1);
		return -ENOMEM;
	}

	memcpy(output_buf, value_start, arg_len);
	output_buf[arg_len] = '\0';

	return 0;
}

/**
 * @brief Quick scan to determine if JSON contains an "id" field
 */
static bool has_id_field(const char *json, size_t length)
{
	return (strstr(json, "\"id\"") != NULL);
}

/**
 * @brief Extract method name from JSON
 */
static int get_method_from_json(const char *json, size_t length, char *method_buf,
				size_t method_buf_size)
{
	const char *method_start = strstr(json, "\"method\"");
	if (!method_start) {
		LOG_ERR("Missing 'method' field in JSON");
		return -EINVAL;
	}

	const char *colon = strchr(method_start, ':');
	if (!colon) {
		return -EINVAL;
	}

	const char *quote1 = strchr(colon, '"');
	if (!quote1) {
		return -EINVAL;
	}

	const char *quote2 = strchr(quote1 + 1, '"');
	if (!quote2) {
		return -EINVAL;
	}

	size_t method_len = quote2 - quote1 - 1;
	if (method_len >= method_buf_size) {
		LOG_ERR("Method name too long");
		return -EINVAL;
	}

	memcpy(method_buf, quote1 + 1, method_len);
	method_buf[method_len] = '\0';

	return 0;
}

/* =============================================================================
 * Request Parsers
 * =============================================================================
 */

static int parse_initialize(char *json_buffer, size_t length,
				struct mcp_initialize_request **out_request)
{
	struct initialize_request_json parsed;
	struct mcp_initialize_request *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, initialize_request_json_descr,
				 ARRAY_SIZE(initialize_request_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse initialize request JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate required fields */
	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "initialize") != 0) {
		LOG_ERR("Method mismatch: expected 'initialize', got '%s'",
			parsed.method ? parsed.method : "NULL");
		return -EINVAL;
	}

	if (!parsed.params.protocolVersion) {
		LOG_ERR("Missing protocolVersion in params");
		return -EINVAL;
	}

	if (!parsed.params.clientInfo.name || !parsed.params.clientInfo.version) {
		LOG_ERR("Missing required clientInfo fields");
		return -EINVAL;
	}

	/* Allocate request structure */
	request = (struct mcp_initialize_request *)mcp_alloc(sizeof(struct mcp_initialize_request));
	if (!request) {
		LOG_ERR("Failed to allocate initialize request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;

	*out_request = request;

	return 0;
}

static int parse_tools_list(char *json_buffer, size_t length,
				struct mcp_tools_list_request **out_request)
{
	struct tools_list_request_json parsed;
	struct mcp_tools_list_request *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, tools_list_request_json_descr,
				 ARRAY_SIZE(tools_list_request_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/list request JSON: %d", ret);
		return -EINVAL;
	}

	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/list") != 0) {
		LOG_ERR("Method mismatch: expected 'tools/list'");
		return -EINVAL;
	}

	request = (struct mcp_tools_list_request *)mcp_alloc(sizeof(struct mcp_tools_list_request));
	if (!request) {
		LOG_ERR("Failed to allocate tools/list request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;
	*out_request = request;

	return 0;
}

static int parse_tools_call(const char *original_json, char *json_buffer, size_t length,
				struct mcp_tools_call_request **out_request)
{
	struct tools_call_request_json parsed;
	struct mcp_tools_call_request *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, tools_call_request_json_descr,
				 ARRAY_SIZE(tools_call_request_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/call request JSON: %d", ret);
		return -EINVAL;
	}

	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/call") != 0) {
		LOG_ERR("Method mismatch: expected 'tools/call'");
		return -EINVAL;
	}

	if (!parsed.params.name) {
		LOG_ERR("Missing required 'name' parameter");
		return -EINVAL;
	}

	request = (struct mcp_tools_call_request *)mcp_alloc(sizeof(struct mcp_tools_call_request));
	if (!request) {
		LOG_ERR("Failed to allocate tools/call request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;

	strncpy(request->name, parsed.params.name, CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
	request->name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

	/* Extract arguments as raw JSON */
	ret = extract_arguments_field(original_json, length,
					   request->arguments, CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN);
	if (ret != 0) {
		LOG_ERR("Failed to extract arguments: %d", ret);
		mcp_free(request);
		return ret;
	}

	*out_request = request;

	return 0;
}

static int parse_notification_initialized(char *json_buffer, size_t length,
					  struct mcp_client_notification **out_notification)
{
	struct jsonrpc_notification parsed;
	struct mcp_client_notification *notification;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, jsonrpc_notification_descr,
				 ARRAY_SIZE(jsonrpc_notification_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse notification JSON: %d", ret);
		return -EINVAL;
	}

	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "notifications/initialized") != 0) {
		LOG_ERR("Method mismatch: expected 'notifications/initialized'");
		return -EINVAL;
	}

	notification = (struct mcp_client_notification *)mcp_alloc(sizeof(struct mcp_client_notification));
	if (!notification) {
		LOG_ERR("Failed to allocate notification");
		return -ENOMEM;
	}

	notification->method = MCP_NOTIF_INITIALIZED;

	*out_notification = notification;

	return 0;
}

static int parse_notification_cancelled(char *json_buffer, size_t length,
					struct mcp_cancelled_notification **out_notification)
{
	struct cancelled_notification_json parsed;
	struct mcp_cancelled_notification *notification;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, cancelled_notification_json_descr,
				 ARRAY_SIZE(cancelled_notification_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse cancelled notification JSON: %d", ret);
		return -EINVAL;
	}

	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "notifications/cancelled") != 0) {
		LOG_ERR("Method mismatch: expected 'notifications/cancelled'");
		return -EINVAL;
	}

	if (!parsed.params.requestId) {
		LOG_ERR("Missing required 'requestId' parameter");
		return -EINVAL;
	}

	notification = (struct mcp_cancelled_notification *)mcp_alloc(
		sizeof(struct mcp_cancelled_notification));
	if (!notification) {
		LOG_ERR("Failed to allocate cancelled notification");
		return -ENOMEM;
	}

	notification->request_id = (uint32_t)atoi(parsed.params.requestId);

	if (parsed.params.reason) {
		strncpy(notification->reason, parsed.params.reason, sizeof(notification->reason) - 1);
		notification->reason[sizeof(notification->reason) - 1] = '\0';
	} else {
		notification->reason[0] = '\0';
	}

	*out_notification = notification;

	return 0;
}

static int parse_notification_progress(char *json_buffer, size_t length,
					   struct mcp_progress_notification **out_notification)
{
	struct progress_notification_json parsed;
	struct mcp_progress_notification *notification;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, progress_notification_json_descr,
				 ARRAY_SIZE(progress_notification_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse progress notification JSON: %d", ret);
		return -EINVAL;
	}

	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "notifications/progress") != 0) {
		LOG_ERR("Method mismatch: expected 'notifications/progress'");
		return -EINVAL;
	}

	if (!parsed.params.progressToken) {
		LOG_ERR("Missing required 'progressToken' parameter");
		return -EINVAL;
	}

	notification = (struct mcp_progress_notification *)mcp_alloc(
		sizeof(struct mcp_progress_notification));
	if (!notification) {
		LOG_ERR("Failed to allocate progress notification");
		return -ENOMEM;
	}

	strncpy(notification->progress_token, parsed.params.progressToken,
		sizeof(notification->progress_token) - 1);
	notification->progress_token[sizeof(notification->progress_token) - 1] = '\0';

	notification->progress = parsed.params.progress;
	notification->total = parsed.params.total;  /* May be 0 if not provided */

	if (parsed.params.message) {
		strncpy(notification->message, parsed.params.message,
			sizeof(notification->message) - 1);
		notification->message[sizeof(notification->message) - 1] = '\0';
	} else {
		notification->message[0] = '\0';
	}

	*out_notification = notification;

	return 0;
}

/* =============================================================================
 * Main Parser Entry Point
 * =============================================================================
 */

int mcp_json_parse_request(const char *json, size_t length,
			   enum mcp_queue_msg_type *type, void **data)
{
	char *json_buffer = NULL;
	char method[MAX_METHOD_NAME_LEN];
	bool is_notification;
	int ret;

	if (!json || length == 0 || !type || !data) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	*type = MCP_MSG_UNKNOWN;
	*data = NULL;

	/* Determine if this is a notification (no id field) */
	is_notification = !has_id_field(json, length);

	/* Extract method name */
	ret = get_method_from_json(json, length, method, sizeof(method));
	if (ret != 0) {
		LOG_ERR("Failed to extract method: %d", ret);
		return ret;
	}

	LOG_DBG("Method: %s, is_notification: %d", method, is_notification);

	/* Create mutable copy for json_obj_parse */
	json_buffer = create_json_copy(json, length);
	if (!json_buffer) {
		LOG_ERR("Failed to allocate JSON buffer");
		return -ENOMEM;
	}

	/* Route to appropriate parser */
	if (is_notification) {
		if (strcmp(method, "notifications/initialized") == 0) {
			*type = MCP_MSG_NOTIFICATION;
			ret = parse_notification_initialized(json_buffer, length,
								 (struct mcp_client_notification **)data);
		} else if (strcmp(method, "notifications/cancelled") == 0) {
			*type = MCP_MSG_NOTIF_CANCELLED;
			ret = parse_notification_cancelled(json_buffer, length,
							  (struct mcp_cancelled_notification **)data);
		} else if (strcmp(method, "notifications/progress") == 0) {
			*type = MCP_MSG_NOTIF_PROGRESS;
			ret = parse_notification_progress(json_buffer, length,
							 (struct mcp_progress_notification **)data);
		} else {
			LOG_ERR("Unknown notification method: %s", method);
			ret = -ENOTSUP;
		}
	} else {
		/* Requests */
		if (strcmp(method, "initialize") == 0) {
			*type = MCP_MSG_REQUEST_INITIALIZE;
			ret = parse_initialize(json_buffer, length,
						   (struct mcp_initialize_request **)data);
		}
		else if (strcmp(method, "tools/list") == 0) {
			*type = MCP_MSG_REQUEST_TOOLS_LIST;
			ret = parse_tools_list(json_buffer, length,
						   (struct mcp_tools_list_request **)data);
		} else if (strcmp(method, "tools/call") == 0) {
			*type = MCP_MSG_REQUEST_TOOLS_CALL;
			ret = parse_tools_call(json, json_buffer, length,
						   (struct mcp_tools_call_request **)data);
		}
		else {
			LOG_ERR("Unknown request method: %s", method);
			ret = -ENOTSUP;
		}
	}

	mcp_free(json_buffer);

	if (ret != 0) {
		LOG_ERR("Failed to parse %s: %d", method, ret);
		if (*data) {
			mcp_free(*data);
			*data = NULL;
		}
		return ret;
	}

	if (!*data) {
		LOG_ERR("Parse succeeded but data is NULL");
		return -EINVAL;
	}

	return 0;
}

/* =============================================================================
 * Response Serializers
 * =============================================================================
 */

int mcp_json_serialize_initialize_response(const struct mcp_initialize_response *resp,
					   char *buffer, size_t buffer_size)
{
	char capabilities_buf[512];
	int cap_len = 0;
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	/* Build capabilities object */
	cap_len = snprintf(capabilities_buf, sizeof(capabilities_buf), "{");

	bool first = true;

	if (resp->capabilities & MCP_TOOLS) {
		cap_len += snprintf(capabilities_buf + cap_len,
					sizeof(capabilities_buf) - cap_len,
					"%s\"tools\": {\"listChanged\":false}",
					first ? "" : ",");
		first = false;
	}

	cap_len += snprintf(capabilities_buf + cap_len,
				sizeof(capabilities_buf) - cap_len, "}");

	/* Build full response */
	len = snprintf(buffer, buffer_size,
			   "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{"
			   "\"protocolVersion\":\"2024-11-05\","
			   "\"capabilities\":%s,"
			   "\"serverInfo\":{"
			   "\"name\":\"%s\","
			   "\"version\":\"%s\""
			   "}}}",
			   resp->request_id,
			   capabilities_buf,
			   CONFIG_MCP_SERVER_INFO_NAME,
			   CONFIG_MCP_SERVER_INFO_VERSION);

	if (len < 0) {
		LOG_ERR("snprintf failed");
		return -EINVAL;
	}

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small (need %d, have %zu)", len + 1, buffer_size);
		return -ENOMEM;
	}

	return len;
}

int mcp_json_serialize_tools_list_response(const struct mcp_tools_list_response *resp,
					   char *buffer, size_t buffer_size)
{
	int len = 0;
	int ret;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	ret = snprintf(buffer, buffer_size,
			   "{\"jsonrpc\": \"2.0\",\"id\": %u,\"result\": {\"tools\":[",
			   resp->request_id);
	if (ret < 0 || ret >= buffer_size) {
		return -ENOMEM;
	}
	len += ret;

	for (int i = 0; i < resp->tool_count; i++) {
		ret = snprintf(buffer + len, buffer_size - len,
				   "%s{\"name\":\"%s\",\"inputSchema\":%s",
				   (i > 0) ? "," : "",
				   resp->tools[i].name,
				   resp->tools[i].input_schema);
		if (ret < 0 || ret >= (buffer_size - len)) {
			return -ENOMEM;
		}
		len += ret;

#ifdef CONFIG_MCP_TOOL_DESC
		if (resp->tools[i].description[0] != '\0') {
			ret = snprintf(buffer + len, buffer_size - len,
					   ",\"description\": \"%s\"",
					   resp->tools[i].description);
			if (ret < 0 || ret >= (buffer_size - len)) {
				return -ENOMEM;
			}
			len += ret;
		}
#endif

		ret = snprintf(buffer + len, buffer_size - len, "}");
		if (ret < 0 || ret >= (buffer_size - len)) {
			return -ENOMEM;
		}
		len += ret;
	}

	ret = snprintf(buffer + len, buffer_size - len, "]}}");
	if (ret < 0 || ret >= (buffer_size - len)) {
		return -ENOMEM;
	}
	len += ret;

	return len;
}

int mcp_json_serialize_tools_call_response(const struct mcp_tools_call_response *resp,
					   char *buffer, size_t buffer_size)
{
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	if (resp->length == 0) {
		LOG_ERR("Empty result");
		return -EINVAL;
	}

	len = snprintf(buffer, buffer_size,
			   "{\"jsonrpc\": \"2.0\",\"id\": %u,\"result\": {%.*s}}",
			   resp->request_id,
			   resp->length,
			   resp->result);

	if (len < 0 || len >= buffer_size) {
		return -ENOMEM;
	}

	return len;
}

int mcp_json_serialize_error_response(const struct mcp_error_response *resp,
					  char *buffer, size_t buffer_size)
{
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	len = snprintf(buffer, buffer_size,
			   "{\"jsonrpc\": \"2.0\",\"id\":%u,\"error\": {\"code\":%d,\"message\":\"%s\"}}",
			   resp->request_id,
			   resp->error_code,
			   resp->error_message);

	if (len < 0 || len >= buffer_size) {
		return -ENOMEM;
	}

	return len;
}
