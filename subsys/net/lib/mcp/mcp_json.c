/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>
#include <string.h>
#include "mcp_json.h"
#include "mcp_common.h"

LOG_MODULE_REGISTER(mcp_json, CONFIG_MCP_LOG_LEVEL);

/* JSON-RPC 2.0 base request structure */
struct jsonrpc_request {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
};

static const struct json_obj_descr jsonrpc_request_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_request, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_request, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_request, method, JSON_TOK_STRING),
};

/* Initialize request params structure */
struct initialize_params {
	const char *protocolVersion;
	const char *clientInfo_name;
	const char *clientInfo_version;
};

static const struct json_obj_descr initialize_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct initialize_params, protocolVersion, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct initialize_params, clientInfo_name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct initialize_params, clientInfo_version, JSON_TOK_STRING),
};

struct initialize_request_full {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
	struct initialize_params params;
};

static const struct json_obj_descr initialize_request_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct initialize_request_full, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct initialize_request_full, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct initialize_request_full, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct initialize_request_full, params, initialize_params_descr),
};

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/* Tools call request params structure */
struct tools_call_params {
	const char *name;
	const char *arguments;
};

static const struct json_obj_descr tools_call_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_call_params, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct tools_call_params, arguments, JSON_TOK_STRING),
};

struct tools_call_request_full {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
	struct tools_call_params params;
};

static const struct json_obj_descr tools_call_request_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_full, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_full, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct tools_call_request_full, method, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct tools_call_request_full, params, tools_call_params_descr),
};
#endif

/* Notification structure (no id field) */
struct jsonrpc_notification {
	const char *jsonrpc;
	const char *method;
};

static const struct json_obj_descr jsonrpc_notification_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, method, JSON_TOK_STRING),
};

/* Helper function to validate JSON-RPC version */
static bool validate_jsonrpc_version(const char *version)
{
	return (version != NULL && strcmp(version, "2.0") == 0);
}

/* Parse initialize request */
static int parse_initialize_request(const char *json, size_t length,
					uint32_t client_id,
					mcp_initialize_request_t **request)
{
	struct initialize_request_full parsed;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse((char *)json, length, initialize_request_descr,
				 ARRAY_SIZE(initialize_request_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse initialize request: %d", ret);
		return ret;
	}

	if (!validate_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid JSON-RPC version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "initialize") != 0) {
		LOG_ERR("Invalid method for initialize request");
		return -EINVAL;
	}

	/* Allocate and populate request structure */
	*request = (mcp_initialize_request_t *)mcp_alloc(sizeof(mcp_initialize_request_t));
	if (!*request) {
		LOG_ERR("Failed to allocate initialize request");
		return -ENOMEM;
	}

	(*request)->request_id = parsed.id;
	(*request)->client_id = client_id;

	LOG_DBG("Parsed initialize request: id=%u, client=%u, protocol=%s",
		(*request)->request_id, (*request)->client_id,
		parsed.params.protocolVersion ? parsed.params.protocolVersion : "unknown");

	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/* Parse tools/list request */
static int parse_tools_list_request(const char *json, size_t length,
					uint32_t client_id,
					mcp_tools_list_request_t **request)
{
	struct jsonrpc_request parsed;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse((char *)json, length, jsonrpc_request_descr,
				 ARRAY_SIZE(jsonrpc_request_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/list request: %d", ret);
		return ret;
	}

	if (!validate_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid JSON-RPC version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/list") != 0) {
		LOG_ERR("Invalid method for tools/list request");
		return -EINVAL;
	}

	/* Allocate and populate request structure */
	*request = (mcp_tools_list_request_t *)mcp_alloc(sizeof(mcp_tools_list_request_t));
	if (!*request) {
		LOG_ERR("Failed to allocate tools/list request");
		return -ENOMEM;
	}

	(*request)->request_id = parsed.id;
	(*request)->client_id = client_id;

	LOG_DBG("Parsed tools/list request: id=%u, client=%u",
		(*request)->request_id, (*request)->client_id);

	return 0;
}

/* Parse tools/call request */
static int parse_tools_call_request(const char *json, size_t length,
					uint32_t client_id,
					mcp_tools_call_request_t **request)
{
	struct tools_call_request_full parsed;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse((char *)json, length, tools_call_request_descr,
				 ARRAY_SIZE(tools_call_request_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/call request: %d", ret);
		return ret;
	}

	if (!validate_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid JSON-RPC version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/call") != 0) {
		LOG_ERR("Invalid method for tools/call request");
		return -EINVAL;
	}

	if (!parsed.params.name) {
		LOG_ERR("Missing tool name in tools/call request");
		return -EINVAL;
	}

	/* Allocate and populate request structure */
	*request = (mcp_tools_call_request_t *)mcp_alloc(sizeof(mcp_tools_call_request_t));
	if (!*request) {
		LOG_ERR("Failed to allocate tools/call request");
		return -ENOMEM;
	}

	(*request)->request_id = parsed.id;
	(*request)->client_id = client_id;

	/* Copy tool name */
	strncpy((*request)->name, parsed.params.name, CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
	(*request)->name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

	/* Copy arguments (already JSON string) */
	if (parsed.params.arguments) {
		strncpy((*request)->arguments, parsed.params.arguments,
			CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1);
		(*request)->arguments[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1] = '\0';
	} else {
		(*request)->arguments[0] = '\0';
	}

	LOG_DBG("Parsed tools/call request: id=%u, client=%u, tool=%s",
		(*request)->request_id, (*request)->client_id, (*request)->name);

	return 0;
}
#endif

/* Parse notifications/initialized */
static int parse_initialized_notification(const char *json, size_t length,
					  uint32_t client_id,
					  mcp_client_notification_t **notification)
{
	struct jsonrpc_notification parsed;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse((char *)json, length, jsonrpc_notification_descr,
				 ARRAY_SIZE(jsonrpc_notification_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse notification: %d", ret);
		return ret;
	}

	if (!validate_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid JSON-RPC version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "notifications/initialized") != 0) {
		LOG_ERR("Invalid method for initialized notification");
		return -EINVAL;
	}

	/* Allocate and populate notification structure */
	*notification = (mcp_client_notification_t *)mcp_alloc(sizeof(mcp_client_notification_t));
	if (!*notification) {
		LOG_ERR("Failed to allocate notification");
		return -ENOMEM;
	}

	(*notification)->client_id = client_id;
	(*notification)->method = MCP_NOTIF_INITIALIZED;

	LOG_DBG("Parsed initialized notification: client=%u", (*notification)->client_id);

	return 0;
}

/* Determine message type by parsing method field */
static int determine_message_type(const char *json, size_t length,
				  mcp_queue_msg_type_t *type, bool *is_notification)
{
	struct jsonrpc_request parsed;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	/* First try to parse as request (with id) */
	ret = json_obj_parse((char *)json, length, jsonrpc_request_descr,
				 ARRAY_SIZE(jsonrpc_request_descr), &parsed);
	if (ret < 0) {
		/* Try parsing as notification (without id) */
		struct jsonrpc_notification notif_parsed;

		memset(&notif_parsed, 0, sizeof(notif_parsed));
		ret = json_obj_parse((char *)json, length, jsonrpc_notification_descr,
					 ARRAY_SIZE(jsonrpc_notification_descr), &notif_parsed);
		if (ret < 0) {
			LOG_ERR("Failed to parse JSON-RPC message: %d", ret);
			return -EINVAL;
		}

		*is_notification = true;

		if (!notif_parsed.method) {
			LOG_ERR("Missing method in notification");
			return -EINVAL;
		}

		if (strcmp(notif_parsed.method, "notifications/initialized") == 0) {
			*type = MCP_MSG_NOTIFICATION;
			return 0;
		}

		LOG_ERR("Unknown notification method: %s", notif_parsed.method);
		return -EINVAL;
	}

	*is_notification = false;

	if (!parsed.method) {
		LOG_ERR("Missing method in request");
		return -EINVAL;
	}

	/* Determine request type based on method */
	if (strcmp(parsed.method, "initialize") == 0) {
		*type = MCP_MSG_REQUEST_INITIALIZE;
	}
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	else if (strcmp(parsed.method, "tools/list") == 0) {
		*type = MCP_MSG_REQUEST_TOOLS_LIST;
	} else if (strcmp(parsed.method, "tools/call") == 0) {
		*type = MCP_MSG_REQUEST_TOOLS_CALL;
	}
#endif
	else {
		LOG_ERR("Unknown method: %s", parsed.method);
		return -ENOTSUP;
	}

	return 0;
}

/* Main JSON parsing function */
int mcp_json_parse_request(const char *json, size_t length,
			   uint32_t client_id,
			   mcp_queue_msg_type_t *type, void **data)
{
	bool is_notification = false;
	int ret;

	if (!json || length == 0 || !type || !data) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	if (client_id == 0) {
		LOG_ERR("Invalid client_id");
		return -EINVAL;
	}

	/* Determine message type */
	ret = determine_message_type(json, length, type, &is_notification);
	if (ret != 0) {
		LOG_ERR("Failed to determine message type: %d", ret);
		return ret;
	}

	/* Parse based on type */
	if (is_notification) {
		switch (*type) {
		case MCP_MSG_NOTIFICATION:
			ret = parse_initialized_notification(json, length, client_id,
								 (mcp_client_notification_t **)data);
			break;
		default:
			LOG_ERR("Unknown notification type: %d", *type);
			return -EINVAL;
		}
	} else {
		switch (*type) {
		case MCP_MSG_REQUEST_INITIALIZE:
			ret = parse_initialize_request(json, length, client_id,
							   (mcp_initialize_request_t **)data);
			break;

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
		case MCP_MSG_REQUEST_TOOLS_LIST:
			ret = parse_tools_list_request(json, length, client_id,
							   (mcp_tools_list_request_t **)data);
			break;

		case MCP_MSG_REQUEST_TOOLS_CALL:
			ret = parse_tools_call_request(json, length, client_id,
							   (mcp_tools_call_request_t **)data);
			break;
#endif

		default:
			LOG_ERR("Unknown request type: %d", *type);
			return -EINVAL;
		}
	}

	if (ret != 0) {
		LOG_ERR("Failed to parse message: %d", ret);
		return ret;
	}

	return 0;
}

/* JSON serialization implementations */

int mcp_json_serialize_initialize_response(const mcp_initialize_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	int len;
	const char *tools_capability = (resp->capabilities & MCP_TOOLS) ? "true" : "false";

	len = snprintf(buffer, buffer_size,
			  "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{"
			  "\"protocolVersion\":\"2024-11-05\","
			  "\"capabilities\":{\"tools\":%s},"
			  "\"serverInfo\":{\"name\":\"%s\",\"version\":\"%s\""
#ifdef CONFIG_MCP_SERVER_INFO_TITLE
			  ",\"title\":\"%s\""
#endif
#ifdef CONFIG_MCP_SERVER_INFO_INSTRUCTIONS
			  ",\"instructions\":\"%s\""
#endif
			  "}}}",
			  resp->request_id,
			  tools_capability,
			  CONFIG_MCP_SERVER_INFO_NAME,
			  CONFIG_MCP_SERVER_INFO_VERSION
#ifdef CONFIG_MCP_SERVER_INFO_TITLE
			  , CONFIG_MCP_SERVER_INFO_TITLE_VALUE
#endif
#ifdef CONFIG_MCP_SERVER_INFO_INSTRUCTIONS
			  , CONFIG_MCP_SERVER_INFO_INSTRUCTIONS_VALUE
#endif
			  );

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small for initialize response");
		return -ENOMEM;
	}

	return len;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
int mcp_json_serialize_tools_list_response(const mcp_tools_list_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	int len = 0;
	int ret;

	ret = snprintf(buffer, buffer_size,
			  "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"tools\":[",
			  resp->request_id);
	if (ret >= buffer_size) {
		return -ENOMEM;
	}
	len += ret;

	for (int i = 0; i < resp->tool_count; i++) {
		ret = snprintf(buffer + len, buffer_size - len,
				  "%s{\"name\":\"%s\",\"inputSchema\":%s",
				  (i > 0) ? "," : "",
				  resp->tools[i].name,
				  resp->tools[i].input_schema);
		if (ret >= (buffer_size - len)) {
			return -ENOMEM;
		}
		len += ret;

#ifdef CONFIG_MCP_TOOL_DESC
		if (resp->tools[i].description[0] != '\0') {
			ret = snprintf(buffer + len, buffer_size - len,
					  ",\"description\":\"%s\"",
					  resp->tools[i].description);
			if (ret >= (buffer_size - len)) {
				return -ENOMEM;
			}
			len += ret;
		}
#endif

#ifdef CONFIG_MCP_TOOL_TITLE
		if (resp->tools[i].title[0] != '\0') {
			ret = snprintf(buffer + len, buffer_size - len,
					  ",\"title\":\"%s\"",
					  resp->tools[i].title);
			if (ret >= (buffer_size - len)) {
				return -ENOMEM;
			}
			len += ret;
		}
#endif

#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
		if (resp->tools[i].output_schema[0] != '\0') {
			ret = snprintf(buffer + len, buffer_size - len,
					  ",\"outputSchema\":%s",
					  resp->tools[i].output_schema);
			if (ret >= (buffer_size - len)) {
				return -ENOMEM;
			}
			len += ret;
		}
#endif

		ret = snprintf(buffer + len, buffer_size - len, "}");
		if (ret >= (buffer_size - len)) {
			return -ENOMEM;
		}
		len += ret;
	}

	ret = snprintf(buffer + len, buffer_size - len, "]}}");
	if (ret >= (buffer_size - len)) {
		return -ENOMEM;
	}
	len += ret;

	return len;
}

int mcp_json_serialize_tools_call_response(const mcp_tools_call_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	int len;

	/* The result is already a JSON string, just wrap it in JSON-RPC response */
	len = snprintf(buffer, buffer_size,
			  "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%.*s}",
			  resp->request_id,
			  resp->length,
			  resp->result);

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small for tools call response");
		return -ENOMEM;
	}

	return len;
}
#endif

int mcp_json_serialize_error_response(const mcp_error_response_t *resp,
					  char *buffer, size_t buffer_size)
{
	int len;

	len = snprintf(buffer, buffer_size,
			  "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
			  resp->request_id,
			  resp->error_code,
			  resp->error_message);

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small for error response");
		return -ENOMEM;
	}

	return len;
}
