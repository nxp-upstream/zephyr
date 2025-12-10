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

/* =============================================================================
 * JSON-RPC 2.0 Structure Definitions
 * =============================================================================
 */

/* Base JSON-RPC request structure (with id for requests) */
struct jsonrpc_base {
	const char *jsonrpc;
	uint32_t id;
	const char *method;
};

static const struct json_obj_descr jsonrpc_base_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_base, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_base, id, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_base, method, JSON_TOK_STRING),
};

/* JSON-RPC notification structure (no id field) */
struct jsonrpc_notification {
	const char *jsonrpc;
	const char *method;
};

static const struct json_obj_descr jsonrpc_notification_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct jsonrpc_notification, method, JSON_TOK_STRING),
};

/* Initialize request parameters */
struct initialize_params {
	const char *protocolVersion;
};

static const struct json_obj_descr initialize_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct initialize_params, protocolVersion, JSON_TOK_STRING),
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

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/* Tools call request parameters */
struct tools_call_params {
	const char *name;
	const char *arguments;
};

static const struct json_obj_descr tools_call_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct tools_call_params, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct tools_call_params, arguments, JSON_TOK_STRING),
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
#endif

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

/**
 * @brief Create a mutable copy of a JSON string
 *
 * json_obj_parse modifies the input buffer, so we need mutable copies
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

/**
 * @brief Check if JSON string contains an "id" field
 */
static bool has_id_field(const char *json)
{
	if (!json) {
		return false;
	}
	return (strstr(json, "\"id\"") != NULL);
}

/**
 * @brief Validate JSON-RPC version string
 */
static bool is_valid_jsonrpc_version(const char *version)
{
	if (!version) {
		return false;
	}
	return (strcmp(version, "2.0") == 0);
}

/* =============================================================================
 * Request Parsing Functions
 * =============================================================================
 */

/**
 * @brief Parse initialize request
 */
static int parse_initialize(char *json_buffer, size_t length, uint32_t client_id,
			    mcp_initialize_request_t **out_request)
{
	struct initialize_request_json parsed;
	mcp_initialize_request_t *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, initialize_request_json_descr,
			     ARRAY_SIZE(initialize_request_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse initialize request JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate parsed fields */
	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "initialize") != 0) {
		LOG_ERR("Method mismatch: expected 'initialize', got '%s'",
			parsed.method ? parsed.method : "NULL");
		return -EINVAL;
	}

	/* Allocate output structure */
	request = (mcp_initialize_request_t *)mcp_alloc(sizeof(mcp_initialize_request_t));
	if (!request) {
		LOG_ERR("Failed to allocate initialize request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;
	request->client_id = client_id;

	*out_request = request;

	LOG_DBG("Parsed initialize request: id=%u, client=%u", request->request_id, client_id);
	return 0;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/**
 * @brief Parse tools/list request
 */
static int parse_tools_list(char *json_buffer, size_t length, uint32_t client_id,
			     mcp_tools_list_request_t **out_request)
{
	struct jsonrpc_base parsed;
	mcp_tools_list_request_t *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, jsonrpc_base_descr,
			     ARRAY_SIZE(jsonrpc_base_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/list request JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate parsed fields */
	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/list") != 0) {
		LOG_ERR("Method mismatch: expected 'tools/list', got '%s'",
			parsed.method ? parsed.method : "NULL");
		return -EINVAL;
	}

	/* Allocate output structure */
	request = (mcp_tools_list_request_t *)mcp_alloc(sizeof(mcp_tools_list_request_t));
	if (!request) {
		LOG_ERR("Failed to allocate tools/list request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;
	request->client_id = client_id;

	*out_request = request;

	LOG_DBG("Parsed tools/list request: id=%u, client=%u", request->request_id, client_id);
	return 0;
}

/**
 * @brief Parse tools/call request
 */
static int parse_tools_call(char *json_buffer, size_t length, uint32_t client_id,
			     mcp_tools_call_request_t **out_request)
{
	struct tools_call_request_json parsed;
	mcp_tools_call_request_t *request;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, tools_call_request_json_descr,
			     ARRAY_SIZE(tools_call_request_json_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse tools/call request JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate parsed fields */
	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "tools/call") != 0) {
		LOG_ERR("Method mismatch: expected 'tools/call', got '%s'",
			parsed.method ? parsed.method : "NULL");
		return -EINVAL;
	}

	if (!parsed.params.name) {
		LOG_ERR("Missing required 'name' parameter");
		return -EINVAL;
	}

	/* Allocate output structure */
	request = (mcp_tools_call_request_t *)mcp_alloc(sizeof(mcp_tools_call_request_t));
	if (!request) {
		LOG_ERR("Failed to allocate tools/call request");
		return -ENOMEM;
	}

	request->request_id = parsed.id;
	request->client_id = client_id;

	/* Copy tool name */
	strncpy(request->name, parsed.params.name, CONFIG_MCP_TOOL_NAME_MAX_LEN - 1);
	request->name[CONFIG_MCP_TOOL_NAME_MAX_LEN - 1] = '\0';

	/* Copy arguments (may be NULL) */
	if (parsed.params.arguments) {
		strncpy(request->arguments, parsed.params.arguments,
			CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1);
		request->arguments[CONFIG_MCP_TOOL_INPUT_ARGS_MAX_LEN - 1] = '\0';
	} else {
		request->arguments[0] = '\0';
	}

	*out_request = request;

	LOG_DBG("Parsed tools/call request: id=%u, client=%u, tool=%s",
		request->request_id, client_id, request->name);
	return 0;
}
#endif

/**
 * @brief Parse notifications/initialized notification
 */
static int parse_notification_initialized(char *json_buffer, size_t length, uint32_t client_id,
					  mcp_client_notification_t **out_notification)
{
	struct jsonrpc_notification parsed;
	mcp_client_notification_t *notification;
	int ret;

	memset(&parsed, 0, sizeof(parsed));

	ret = json_obj_parse(json_buffer, length, jsonrpc_notification_descr,
			     ARRAY_SIZE(jsonrpc_notification_descr), &parsed);
	if (ret < 0) {
		LOG_ERR("Failed to parse notification JSON: %d", ret);
		return -EINVAL;
	}

	/* Validate parsed fields */
	if (!parsed.jsonrpc || !is_valid_jsonrpc_version(parsed.jsonrpc)) {
		LOG_ERR("Invalid or missing jsonrpc version");
		return -EINVAL;
	}

	if (!parsed.method || strcmp(parsed.method, "notifications/initialized") != 0) {
		LOG_ERR("Method mismatch: expected 'notifications/initialized', got '%s'",
			parsed.method ? parsed.method : "NULL");
		return -EINVAL;
	}

	/* Allocate output structure */
	notification = (mcp_client_notification_t *)mcp_alloc(sizeof(mcp_client_notification_t));
	if (!notification) {
		LOG_ERR("Failed to allocate notification");
		return -ENOMEM;
	}

	notification->client_id = client_id;
	notification->method = MCP_NOTIF_INITIALIZED;

	*out_notification = notification;

	LOG_DBG("Parsed initialized notification: client=%u", client_id);
	return 0;
}

/**
 * @brief Determine the method from JSON to route to correct parser
 */
static int get_method_from_json(char *json_buffer, size_t length, const char **method,
				bool *is_notification)
{
	struct jsonrpc_base parsed_req;
	struct jsonrpc_notification parsed_notif;
	int ret;

	/* Check if this has an id field (request vs notification) */
	*is_notification = !has_id_field(json_buffer);

	if (*is_notification) {
		/* Parse as notification */
		memset(&parsed_notif, 0, sizeof(parsed_notif));
		ret = json_obj_parse(json_buffer, length, jsonrpc_notification_descr,
				     ARRAY_SIZE(jsonrpc_notification_descr), &parsed_notif);
		if (ret < 0) {
			LOG_ERR("Failed to parse as notification: %d", ret);
			return -EINVAL;
		}

		if (!parsed_notif.method) {
			LOG_ERR("Missing method in notification");
			return -EINVAL;
		}

		*method = parsed_notif.method;
	} else {
		/* Parse as request */
		memset(&parsed_req, 0, sizeof(parsed_req));
		ret = json_obj_parse(json_buffer, length, jsonrpc_base_descr,
				     ARRAY_SIZE(jsonrpc_base_descr), &parsed_req);
		if (ret < 0) {
			LOG_ERR("Failed to parse as request: %d", ret);
			return -EINVAL;
		}

		if (!parsed_req.method) {
			LOG_ERR("Missing method in request");
			return -EINVAL;
		}

		*method = parsed_req.method;
	}

	return 0;
}

/* =============================================================================
 * Public API Implementation
 * =============================================================================
 */

/**
 * @brief Parse JSON-RPC request into MCP message
 */
int mcp_json_parse_request(const char *json, size_t length,
			   uint32_t client_id,
			   mcp_queue_msg_type_t *type, void **data)
{
	char *json_buffer1 = NULL;
	char *json_buffer2 = NULL;
	const char *method = NULL;
	bool is_notification = false;
	int ret;

	/* Validate parameters */
	if (!json || length == 0) {
		LOG_ERR("Invalid JSON input");
		return -EINVAL;
	}

	if (!type || !data) {
		LOG_ERR("Invalid output parameters");
		return -EINVAL;
	}

	if (client_id == 0) {
		LOG_ERR("Invalid client_id");
		return -EINVAL;
	}

	/* Initialize outputs */
	*type = 0;
	*data = NULL;

	/* Create first working copy to determine method */
	json_buffer1 = create_json_copy(json, length);
	if (!json_buffer1) {
		LOG_ERR("Failed to allocate JSON buffer");
		return -ENOMEM;
	}

	/* Determine method and message type */
	ret = get_method_from_json(json_buffer1, length, &method, &is_notification);
	if (ret != 0) {
		LOG_ERR("Failed to determine method: %d", ret);
		mcp_free(json_buffer1);
		return ret;
	}

	LOG_DBG("Method: %s, is_notification: %d", method, is_notification);

	/* Create fresh copy for actual parsing (first copy is now corrupted) */
	json_buffer2 = create_json_copy(json, length);
	if (!json_buffer2) {
		LOG_ERR("Failed to allocate second JSON buffer");
		mcp_free(json_buffer1);
		return -ENOMEM;
	}

	/* Route to appropriate parser based on method */
	if (is_notification) {
		if (strcmp(method, "notifications/initialized") == 0) {
			*type = MCP_MSG_NOTIFICATION;
			ret = parse_notification_initialized(json_buffer2, length, client_id,
							     (mcp_client_notification_t **)data);
		} else {
			LOG_ERR("Unknown notification method: %s", method);
			ret = -EINVAL;
		}
	} else {
		if (strcmp(method, "initialize") == 0) {
			*type = MCP_MSG_REQUEST_INITIALIZE;
			ret = parse_initialize(json_buffer2, length, client_id,
					       (mcp_initialize_request_t **)data);
		}
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
		else if (strcmp(method, "tools/list") == 0) {
			*type = MCP_MSG_REQUEST_TOOLS_LIST;
			ret = parse_tools_list(json_buffer2, length, client_id,
					       (mcp_tools_list_request_t **)data);
		} else if (strcmp(method, "tools/call") == 0) {
			*type = MCP_MSG_REQUEST_TOOLS_CALL;
			ret = parse_tools_call(json_buffer2, length, client_id,
					       (mcp_tools_call_request_t **)data);
		}
#endif
		else {
			LOG_ERR("Unknown request method: %s", method);
			ret = -ENOTSUP;
		}
	}

	/* Cleanup */
	mcp_free(json_buffer1);
	mcp_free(json_buffer2);

	if (ret != 0) {
		LOG_ERR("Failed to parse %s: %d", method, ret);
		if (*data) {
			mcp_free(*data);
			*data = NULL;
		}
		return ret;
	}

	/* Verify data was allocated */
	if (!*data) {
		LOG_ERR("Parse succeeded but data is NULL");
		return -EINVAL;
	}

	LOG_DBG("Successfully parsed %s", method);
	return 0;
}

/* =============================================================================
 * JSON Serialization Functions
 * =============================================================================
 */

/**
 * @brief Serialize initialize response to JSON (MCP Spec Compliant)
 */
int mcp_json_serialize_initialize_response(const mcp_initialize_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	char capabilities_buf[256];
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	/* Build capabilities object - per MCP spec, capabilities are objects */
	/* If not supported, omit the field entirely */
	if (resp->capabilities & MCP_TOOLS) {
		snprintf(capabilities_buf, sizeof(capabilities_buf), "\"tools\":{}");
	} else {
		capabilities_buf[0] = '\0';  /* Empty - no capabilities */
	}

	/* Build serverInfo object */
	char serverinfo_buf[512];
	int si_len = snprintf(serverinfo_buf, sizeof(serverinfo_buf),
			      "\"name\":\"%s\",\"version\":\"%s\"",
			      CONFIG_MCP_SERVER_INFO_NAME,
			      CONFIG_MCP_SERVER_INFO_VERSION);

#ifdef CONFIG_MCP_SERVER_INFO_TITLE
	si_len += snprintf(serverinfo_buf + si_len, sizeof(serverinfo_buf) - si_len,
			   ",\"title\":\"%s\"",
			   CONFIG_MCP_SERVER_INFO_TITLE_VALUE);
#endif

#ifdef CONFIG_MCP_SERVER_INFO_INSTRUCTIONS
	si_len += snprintf(serverinfo_buf + si_len, sizeof(serverinfo_buf) - si_len,
			   ",\"instructions\":\"%s\"",
			   CONFIG_MCP_SERVER_INFO_INSTRUCTIONS_VALUE);
#endif

	/* Build complete response */
	len = snprintf(buffer, buffer_size,
		       "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{"
		       "\"protocolVersion\":\"2024-11-05\","
		       "\"capabilities\":{%s},"
		       "\"serverInfo\":{%s}"
		       "}}",
		       resp->request_id,
		       capabilities_buf,
		       serverinfo_buf);

	if (len < 0) {
		LOG_ERR("snprintf failed");
		return -EINVAL;
	}

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small (need %d, have %zu)", len + 1, buffer_size);
		return -ENOMEM;
	}

	LOG_DBG("%s", buffer);

	return len;
}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
/**
 * @brief Serialize tools/list response to JSON
 */
int mcp_json_serialize_tools_list_response(const mcp_tools_list_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	size_t offset = 0;
	int ret;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	/* Start JSON-RPC response */
	ret = snprintf(buffer + offset, buffer_size - offset,
		       "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"tools\":[",
		       resp->request_id);
	if (ret < 0 || ret >= (buffer_size - offset)) {
		return -ENOMEM;
	}
	offset += ret;

	/* Add each tool */
	for (int i = 0; i < resp->tool_count; i++) {
		/* Tool opening */
		ret = snprintf(buffer + offset, buffer_size - offset,
			       "%s{\"name\":\"%s\",\"inputSchema\":%s",
			       (i > 0) ? "," : "",
			       resp->tools[i].name,
			       resp->tools[i].input_schema);
		if (ret < 0 || ret >= (buffer_size - offset)) {
			return -ENOMEM;
		}
		offset += ret;

#ifdef CONFIG_MCP_TOOL_DESC
		if (resp->tools[i].description[0] != '\0') {
			ret = snprintf(buffer + offset, buffer_size - offset,
				       ",\"description\":\"%s\"",
				       resp->tools[i].description);
			if (ret < 0 || ret >= (buffer_size - offset)) {
				return -ENOMEM;
			}
			offset += ret;
		}
#endif

#ifdef CONFIG_MCP_TOOL_TITLE
		if (resp->tools[i].title[0] != '\0') {
			ret = snprintf(buffer + offset, buffer_size - offset,
				       ",\"title\":\"%s\"",
				       resp->tools[i].title);
			if (ret < 0 || ret >= (buffer_size - offset)) {
				return -ENOMEM;
			}
			offset += ret;
		}
#endif

#ifdef CONFIG_MCP_TOOL_OUTPUT_SCHEMA
		if (resp->tools[i].output_schema[0] != '\0') {
			ret = snprintf(buffer + offset, buffer_size - offset,
				       ",\"outputSchema\":%s",
				       resp->tools[i].output_schema);
			if (ret < 0 || ret >= (buffer_size - offset)) {
				return -ENOMEM;
			}
			offset += ret;
		}
#endif

		/* Close tool object */
		ret = snprintf(buffer + offset, buffer_size - offset, "}");
		if (ret < 0 || ret >= (buffer_size - offset)) {
			return -ENOMEM;
		}
		offset += ret;
	}

	/* Close response */
	ret = snprintf(buffer + offset, buffer_size - offset, "]}}");
	if (ret < 0 || ret >= (buffer_size - offset)) {
		return -ENOMEM;
	}
	offset += ret;

	LOG_DBG("%s", buffer);

	return offset;
}

/**
 * @brief Serialize tools/call response to JSON
 */
int mcp_json_serialize_tools_call_response(const mcp_tools_call_response_t *resp,
					   char *buffer, size_t buffer_size)
{
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	if (resp->length < 0 || resp->length > CONFIG_MCP_TOOL_RESULT_MAX_LEN) {
		LOG_ERR("Invalid result length: %d", resp->length);
		return -EINVAL;
	}

	/* Result should already be valid JSON */
	len = snprintf(buffer, buffer_size,
		       "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"content\":[{\"type\":\"text\",\"text\":%.*s}]}}",
		       resp->request_id,
		       resp->length,
		       resp->result);

	if (len < 0) {
		LOG_ERR("snprintf failed");
		return -EINVAL;
	}

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small (need %d, have %zu)", len + 1, buffer_size);
		return -ENOMEM;
	}

	LOG_DBG("%s", buffer);

	return len;
}
#endif

/**
 * @brief Serialize error response to JSON
 */
int mcp_json_serialize_error_response(const mcp_error_response_t *resp,
				      char *buffer, size_t buffer_size)
{
	int len;

	if (!resp || !buffer || buffer_size == 0) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	len = snprintf(buffer, buffer_size,
		       "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
		       resp->request_id,
		       resp->error_code,
		       resp->error_message);

	if (len < 0) {
		LOG_ERR("snprintf failed");
		return -EINVAL;
	}

	if (len >= buffer_size) {
		LOG_ERR("Buffer too small (need %d, have %zu)", len + 1, buffer_size);
		return -ENOMEM;
	}

	LOG_DBG("%s", buffer);

	return len;
}
