/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Parser: server-side messages
 *   - Requests: initialize, ping, tools/list, tools/call
 *   - Notifications: notifications/initialized, notifications/cancelled
 *
 * Serializers: server-side responses & notifications
 */

#include "mcp_json.h"
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h> /* BIT(), ARRAY_SIZE */
#include <zephyr/logging/log.h>
#include "mcp_common.h"

LOG_MODULE_REGISTER(mcp_json, CONFIG_MCP_LOG_LEVEL);

/*******************************************************************************
 * Helpers
 ******************************************************************************/
static void mcp_safe_strcpy(char *dst, size_t dst_sz, const char *src)
{
	if (!dst || dst_sz == 0) {
		return;
	}

	if (!src) {
		dst[0] = '\0';
		return;
	}

	/* Use snprintf for safe truncation + NUL termination */
	(void)snprintf(dst, dst_sz, "%s", src);
}

/*******************************************************************************
 * Envelope descriptor (jsonrpc, method, id)
 ******************************************************************************/
struct mcp_json_envelope {
	struct json_obj_token jsonrpc;
	struct json_obj_token method;
	struct json_obj_token id_string;
	int64_t id_integer;
};

static const struct json_obj_descr mcp_envelope_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_envelope, jsonrpc, JSON_TOK_OPAQUE),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_envelope, method, JSON_TOK_OPAQUE),
	JSON_OBJ_DESCR_PRIM_NAMED(struct mcp_json_envelope, "id", id_string, JSON_TOK_OPAQUE),
	JSON_OBJ_DESCR_PRIM_NAMED(struct mcp_json_envelope, "id", id_integer, JSON_TOK_INT64),
};

/* Helper to extract string from json_obj_token */
static void extract_token_string(char *dst, size_t dst_sz, const struct json_obj_token *token)
{
	if (!dst || dst_sz == 0 || !token || !token->start || token->length == 0) {
		if (dst && dst_sz > 0) {
			dst[0] = '\0';
		}
		return;
	}

	size_t len = token->length;
	if (len >= dst_sz) {
		len = dst_sz - 1;
	}

	memcpy(dst, token->start, len);
	dst[len] = '\0';
}

/* Map method string to enum */
static enum mcp_method mcp_method_from_string(const char *m, size_t len)
{
	if (!m || len == 0) {
		return MCP_METHOD_UNKNOWN;
	}

	/* Create temporary null-terminated string for comparison */
	char method_buf[64];
	if (len >= sizeof(method_buf)) {
		return MCP_METHOD_UNKNOWN;
	}

	memcpy(method_buf, m, len);
	method_buf[len] = '\0';

	if (strcmp(method_buf, "initialize") == 0) {
		return MCP_METHOD_INITIALIZE;
	} else if (strcmp(method_buf, "ping") == 0) {
		return MCP_METHOD_PING;
	} else if (strcmp(method_buf, "tools/list") == 0) {
		return MCP_METHOD_TOOLS_LIST;
	} else if (strcmp(method_buf, "tools/call") == 0) {
		return MCP_METHOD_TOOLS_CALL;
	} else if (strcmp(method_buf, "notifications/initialized") == 0) {
		return MCP_METHOD_NOTIF_INITIALIZED;
	} else if (strcmp(method_buf, "notifications/cancelled") == 0) {
		return MCP_METHOD_NOTIF_CANCELLED;
	}
	return MCP_METHOD_UNKNOWN;
}

/*******************************************************************************
 * Per-method parsing helpers
 ******************************************************************************/
/* initialize request: { "params": { "protocolVersion": "..." } } */
struct mcp_json_init_req {
	struct {
		const char *protocolVersion;
	} params;
};

static const struct json_obj_descr mcp_init_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_init_req *)0)->params), protocolVersion,
			    JSON_TOK_STRING),
};

static const struct json_obj_descr mcp_init_req_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_init_req, params, mcp_init_params_descr),
};

static int parse_initialize_request(const char *buf, size_t len, struct mcp_message *msg)
{
	struct mcp_json_init_req tmp = {0};
	int ret = json_obj_parse((char *)buf, len, mcp_init_req_descr,
				 ARRAY_SIZE(mcp_init_req_descr), &tmp);
	if (ret < 0) {
		LOG_DBG("Failed to parse initialize request: %d", ret);
		return -EINVAL;
	}
	struct mcp_params_initialize *p = &msg->req.u.initialize;
	memset(p, 0, sizeof(*p));
	if (tmp.params.protocolVersion) {
		mcp_safe_strcpy(p->protocol_version, sizeof(p->protocol_version),
				tmp.params.protocolVersion);
	}
	return 0;
}

/* ping request: we ignore params for now */
static int parse_ping_request(const char *buf, size_t len, struct mcp_message *msg)
{
	(void)buf;
	(void)len;
	struct mcp_params_ping *p = &msg->req.u.ping;
	memset(p, 0, sizeof(*p));
	return 0;
}

/* tools/list request: no params in v1 */
static int parse_tools_list_request(const char *buf, size_t len, struct mcp_message *msg)
{
	(void)buf;
	(void)len;
	struct mcp_params_tools_list *p = &msg->req.u.tools_list;
	memset(p, 0, sizeof(*p));
	return 0;
}

/* --- tools/call request:
 *     {
 *       "jsonrpc":"2.0",
 *       "id":N,
 *       "method":"tools/call",
 *       "params": {
 *          "name":"tool_name",
 *          "arguments": { ... }
 *       }
 *     }
 *
 * We parse "name". For arguments, we *optionally* copy the raw JSON of the
 * "arguments" object into arguments_json.
 */
struct mcp_json_tools_call_req {
	struct {
		const char *name;
		/* arguments object will be extracted via substring scan if needed */
	} params;
};

static const struct json_obj_descr mcp_tools_call_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_tools_call_req *)0)->params), name,
			    JSON_TOK_STRING),
};

static const struct json_obj_descr mcp_tools_call_req_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_tools_call_req, params, mcp_tools_call_params_descr),
};

/* Very simple "extract arguments object" helper:
 * Looks for "arguments":{ ... } at top-level of params and copies the
 * substring for the { ... } part, assuming well-formed JSON.
 *
 * This is not a general JSON parser; it's a small helper tuned for
 * typical MCP tools/call payloads.
 */
static bool extract_arguments_json(const char *buf, size_t len, char *dst, size_t dst_sz)
{
	const char *key = "\"arguments\"";
	const char *p = buf;
	const char *end = buf + len;
	while (p < end) {
		const char *hit = strstr(p, key);
		if (!hit || hit >= end) {
			return false;
		}

		const char *q = hit + strlen(key);
		/* skip whitespace */
		while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
			q++;
		}

		if (q >= end || *q != ':') {
			p = hit + 1;
			continue;
		}

		q++; /* skip ':' */
		while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
			q++;
		}

		if (q >= end || *q != '{') {
			p = hit + 1;
			continue;
		}

		/* q now at opening '{' of object; we need to find matching '}' */
		int depth = 0;
		const char *start = q;
		const char *r = q;
		while (r < end) {
			if (*r == '{') {
				depth++;
			} else if (*r == '}') {
				depth--;
				if (depth == 0) {
					/* object ends at r */
					size_t obj_len = (size_t)(r - start + 1);
					if (obj_len + 1 > dst_sz) {
						/* truncated */
						obj_len = dst_sz - 1;
					}

					memcpy(dst, start, obj_len);
					dst[obj_len] = '\0';
					return true;
				}
			}

			r++;
		}

		/* no matching brace; give up */
		return false;
	}

	return false;
}

static int parse_tools_call_request(const char *buf, size_t len, struct mcp_message *msg)
{
	struct mcp_json_tools_call_req tmp = {0};

	int ret = json_obj_parse((char *)buf, len, mcp_tools_call_req_descr,
				 ARRAY_SIZE(mcp_tools_call_req_descr), &tmp);
	if (ret < 0) {
		return -EINVAL;
	}

	struct mcp_params_tools_call *p = &msg->req.u.tools_call;
	memset(p, 0, sizeof(*p));

	if (tmp.params.name) {
		mcp_safe_strcpy(p->name, sizeof(p->name), tmp.params.name);
	}

	if (extract_arguments_json(buf, len, p->arguments_json, sizeof(p->arguments_json))) {
		p->has_arguments = true;
	} else {
		p->has_arguments = false;
	}

	return 0;
}

static int parse_notif_initialized(const char *buf, size_t len, struct mcp_message *msg)
{
	(void)buf;
	(void)len;

	struct mcp_params_notif_initialized *p = &msg->notif.u.initialized;
	memset(p, 0, sizeof(*p));

	return 0;
}

/* --- notifications/cancelled:
 *     {
 *       "jsonrpc":"2.0",
 *       "method":"notifications/cancelled",
 *       "params": { "requestId": <id>, "reason": "..." }
 *     }
 */
struct mcp_json_cancelled_notif {
	struct {
		struct json_obj_token requestId;
		const char *reason;
	} params;
};

static const struct json_obj_descr mcp_cancelled_params_descr[] = {
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_cancelled_notif *)0)->params),
			    requestId, JSON_TOK_OPAQUE),
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_cancelled_notif *)0)->params), reason,
			    JSON_TOK_STRING),
};

static const struct json_obj_descr mcp_cancelled_notif_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_cancelled_notif, params, mcp_cancelled_params_descr),
};

static int parse_notif_cancelled(const char *buf, size_t len, struct mcp_message *msg)
{
	struct mcp_json_cancelled_notif tmp = {0};

	int ret = json_obj_parse((char *)buf, len, mcp_cancelled_notif_descr,
				 ARRAY_SIZE(mcp_cancelled_notif_descr), &tmp);
	if (ret < 0) {
		return -EINVAL;
	}

	struct mcp_params_notif_cancelled *p = &msg->notif.u.cancelled;
	memset(p, 0, sizeof(*p));

	/* Extract requestId as string (with quotes preserved if string) */
	extract_token_string(p->request_id.string, sizeof(p->request_id.string),
			     &tmp.params.requestId);

	if (tmp.params.reason) {
		mcp_safe_strcpy(p->reason, sizeof(p->reason), tmp.params.reason);
		p->has_reason = true;
	}

	return 0;
}

/*******************************************************************************
 * Public parser API
 ******************************************************************************/
int mcp_json_parse_message(char *buf, size_t len, struct mcp_message *out)
{
	if (!buf || !out || len == 0) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->kind = MCP_MSG_INVALID;
	out->method = MCP_METHOD_UNKNOWN;

	/* Step 1: parse the envelope (jsonrpc, method, id) */
	struct mcp_json_envelope env = {0};
	int ret =
		json_obj_parse(buf, len, mcp_envelope_descr, ARRAY_SIZE(mcp_envelope_descr), &env);
	if (ret < 0) {
		LOG_DBG("Failed to parse envelope: %d", ret);
		return -EINVAL;
	}

	/* Check jsonrpc version */
	char jsonrpc_buf[16];
	extract_token_string(jsonrpc_buf, sizeof(jsonrpc_buf), &env.jsonrpc);
	if (strcmp(jsonrpc_buf, "2.0") != 0) {
		LOG_DBG("Invalid jsonrpc version: %s", jsonrpc_buf);
		return -EINVAL;
	}

	/* Determine presence and type of id:
	 * json_obj_parse returns a bitmask: bit N set if field N decoded.
	 * Fields: 0=jsonrpc, 1=method, 2=id_string, 3=id_integer.
	 */
	bool has_id_string = (ret & BIT(2)) != 0;
	bool has_id_integer = (ret & BIT(3)) != 0;

	LOG_DBG("Parse result: jsonrpc=%d method=%d id_string=%d id_integer=%d",
		(ret & BIT(0)) != 0, (ret & BIT(1)) != 0, has_id_string, has_id_integer);

	/* Store ID as string */
	if (has_id_integer) {
		/* Integer ID: store without quotes (e.g., "123") */
		snprintf(out->id.string, sizeof(out->id.string), "%" PRId64, env.id_integer);
	} else if (has_id_string) {
		/* String ID: store with quotes preserved (e.g., "\"abc\"") */
		char temp[MCP_MAX_ID_LEN - 2];
		extract_token_string(temp, sizeof(temp), &env.id_string);

		/* Add quotes around the string value */
		snprintf(out->id.string, sizeof(out->id.string), "\"%s\"", temp);
	} else {
		/* No ID */
		out->id.string[0] = '\0';
	}

	/* Determine method */
	bool has_method = (ret & BIT(1)) != 0;
	if (has_method) {
		out->method = mcp_method_from_string(env.method.start, env.method.length);
	} else {
		out->method = MCP_METHOD_UNKNOWN;
	}

	/* Classify as request or notification:
	 *
	 * - Request: method with id (params optional).
	 * - Notification: method without id (params optional).
	 */
	if (has_method && (has_id_integer || has_id_string)) {
		out->kind = MCP_MSG_REQUEST;
	} else if (has_method && !has_id_integer && !has_id_string) {
		out->kind = MCP_MSG_NOTIFICATION;
	} else {
		/* Server side: we don't expect responses from the client. */
		return -EINVAL;
	}

	/* Dispatch to per-kind, per-method parsers */
	if (out->kind == MCP_MSG_REQUEST) {
		switch (out->method) {
		case MCP_METHOD_INITIALIZE:
			ret = parse_initialize_request(buf, len, out);
			break;
		case MCP_METHOD_PING:
			ret = parse_ping_request(buf, len, out);
			break;
		case MCP_METHOD_TOOLS_LIST:
			ret = parse_tools_list_request(buf, len, out);
			break;
		case MCP_METHOD_TOOLS_CALL:
			ret = parse_tools_call_request(buf, len, out);
			break;
		default:
			/* Unknown method: let core treat as "method not found". */
			ret = 0;
			break;
		}
	} else if (out->kind == MCP_MSG_NOTIFICATION) {
		switch (out->method) {
		case MCP_METHOD_NOTIF_INITIALIZED:
			ret = parse_notif_initialized(buf, len, out);
			break;
		case MCP_METHOD_NOTIF_CANCELLED:
			ret = parse_notif_cancelled(buf, len, out);
			break;
		default:
			/* Unknown notification: ignore content; core can log. */
			ret = 0;
			break;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/*******************************************************************************
 * Serializers
 ******************************************************************************/

/* Serialization structures for initialize result */
struct mcp_json_server_info {
	const char *name;
	const char *version;
};

struct mcp_json_init_result_inner {
	const char *protocolVersion;
	struct mcp_json_server_info serverInfo;
	const char *capabilities;
};

struct mcp_json_init_result {
	const char *jsonrpc;
	const char *id;
	struct mcp_json_init_result_inner result;
};

static const struct json_obj_descr mcp_json_server_info_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_server_info, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_server_info, version, JSON_TOK_STRING),
};

static const struct json_obj_descr mcp_json_init_result_inner_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_init_result_inner, protocolVersion, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_init_result_inner, serverInfo,
			      mcp_json_server_info_descr),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_init_result_inner, capabilities, JSON_TOK_ENCODED_OBJ),
};

static const struct json_obj_descr mcp_json_init_result_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_init_result, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_init_result, id, JSON_TOK_ENCODED_OBJ),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_init_result, result,
			      mcp_json_init_result_inner_descr),
};

int mcp_json_serialize_initialize_result(char *out, size_t out_len,
					 const struct mcp_request_id *id,
					 const struct mcp_result_initialize *res)
{
	if (!out || !res || out_len == 0) {
		return -EINVAL;
	}

	const char *id_str = (id && id->string[0] != '\0') ? id->string : "null";

	struct mcp_json_init_result json_res = {
		.jsonrpc = "2.0",
		.id = id_str,
		.result = {
			.protocolVersion = res->protocol_version,
			.serverInfo = {
				.name = res->server_name,
				.version = res->server_version,
			},
			.capabilities = (res->has_capabilities && res->capabilities_json[0] != '\0')
					? res->capabilities_json : NULL,
		},
	};

	int ret = json_obj_encode_buf(mcp_json_init_result_descr,
				      ARRAY_SIZE(mcp_json_init_result_descr),
				      &json_res, out, out_len);

	return (ret == 0) ? strlen(out) : ret;
}

/* Serialization structures for ping result */
struct mcp_json_ping_result {
	const char *jsonrpc;
	const char *id;
	struct {
		bool dummy; /* Empty object */
	} result;
};

static const struct json_obj_descr mcp_json_ping_result_inner_descr[] = {
	/* Empty descriptor for empty object */
};

static const struct json_obj_descr mcp_json_ping_result_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_ping_result, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_ping_result, id, JSON_TOK_ENCODED_OBJ),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_ping_result, result,
			      mcp_json_ping_result_inner_descr),
};

int mcp_json_serialize_ping_result(char *out, size_t out_len,
				   const struct mcp_request_id *id,
				   const struct mcp_result_ping *res)
{
	(void)res; /* currently unused; we return empty {} */

	if (!out || out_len == 0) {
		return -EINVAL;
	}

	const char *id_str = (id && id->string[0] != '\0') ? id->string : "null";

	struct mcp_json_ping_result json_res = {
		.jsonrpc = "2.0",
		.id = id_str,
		.result = { .dummy = false },
	};

	int ret = json_obj_encode_buf(mcp_json_ping_result_descr,
				      ARRAY_SIZE(mcp_json_ping_result_descr),
				      &json_res, out, out_len);

	return (ret == 0) ? strlen(out) : ret;
}

/* Serialization structures for tools/list result */
struct mcp_json_tools_list_result {
	const char *jsonrpc;
	const char *id;
	struct {
		const char *tools;  /* This should be the raw JSON array */
	} result;
};

static const struct json_obj_descr mcp_json_tools_list_result_inner_descr[] = {
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_tools_list_result *)0)->result),
			    tools, JSON_TOK_ENCODED_OBJ),
};

static const struct json_obj_descr mcp_json_tools_list_result_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_tools_list_result, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_tools_list_result, id, JSON_TOK_ENCODED_OBJ),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_tools_list_result, result,
			      mcp_json_tools_list_result_inner_descr),
};

int mcp_json_serialize_tools_list_result(char *out, size_t out_len,
					 const struct mcp_request_id *id,
					 const struct mcp_result_tools_list *res)
{
	if (!out || !res || out_len == 0) {
		return -EINVAL;
	}

	const char *id_str = (id && id->string[0] != '\0') ? id->string : "null";

	/* tools_json should already be a complete JSON array: [{"name":"foo",...},...]
	 * If empty, use empty array
	 */
	const char *tools_json = (res->tools_json[0] != '\0') ? res->tools_json : "[]";

	struct mcp_json_tools_list_result json_res = {
		.jsonrpc = "2.0",
		.id = id_str,
		.result = {
			.tools = tools_json,  /* Insert raw JSON array */
		},
	};

	int ret = json_obj_encode_buf(mcp_json_tools_list_result_descr,
				      ARRAY_SIZE(mcp_json_tools_list_result_descr),
				      &json_res, out, out_len);

	return (ret == 0) ? strlen(out) : ret;
}

/* Serialization structures for tools/call result */
struct mcp_json_content_item {
	const char *type;
	const char *text;
};

struct mcp_json_tools_call_result {
	const char *jsonrpc;
	const char *id;
	struct {
		struct mcp_json_content_item content[MCP_MAX_CONTENT_ITEMS];
		size_t content_len;
	} result;
};

static const struct json_obj_descr mcp_json_content_item_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_content_item, type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_content_item, text, JSON_TOK_STRING),
};

static const struct json_obj_descr mcp_json_tools_call_result_inner_descr[] = {
	JSON_OBJ_DESCR_OBJ_ARRAY(__typeof__(((struct mcp_json_tools_call_result *)0)->result),
				 content, MCP_MAX_CONTENT_ITEMS, content_len,
				 mcp_json_content_item_descr,
				 ARRAY_SIZE(mcp_json_content_item_descr)),
};

static const struct json_obj_descr mcp_json_tools_call_result_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_tools_call_result, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_tools_call_result, id, JSON_TOK_ENCODED_OBJ),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_tools_call_result, result,
			      mcp_json_tools_call_result_inner_descr),
};

int mcp_json_serialize_tools_call_result(char *out, size_t out_len,
					 const struct mcp_request_id *id,
					 const struct mcp_result_tools_call *res)
{
	if (!out || !res || out_len == 0) {
		return -EINVAL;
	}

	const char *id_str = (id && id->string[0] != '\0') ? id->string : "null";

	struct mcp_json_tools_call_result json_res = {
		.jsonrpc = "2.0",
		.id = id_str,
		.result = {
			.content_len = res->content.count,
		},
	};

	/* Copy content items */
	for (size_t i = 0; i < res->content.count && i < MCP_MAX_CONTENT_ITEMS; i++) {
		json_res.result.content[i].type = "text";
		json_res.result.content[i].text = res->content.items[i].text;
	}

	int ret = json_obj_encode_buf(mcp_json_tools_call_result_descr,
				      ARRAY_SIZE(mcp_json_tools_call_result_descr),
				      &json_res, out, out_len);

	return (ret == 0) ? strlen(out) : ret;
}

/* Serialization structures for error response */
struct mcp_json_error {
	const char *jsonrpc;
	const char *id;
	struct {
		int32_t code;
		const char *message;
		const char *data;
	} error;
};

static const struct json_obj_descr mcp_json_error_inner_descr[] = {
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_error *)0)->error), code, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_error *)0)->error), message, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(__typeof__(((struct mcp_json_error *)0)->error), data, JSON_TOK_ENCODED_OBJ),
};

static const struct json_obj_descr mcp_json_error_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct mcp_json_error, jsonrpc, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct mcp_json_error, id, JSON_TOK_ENCODED_OBJ),
	JSON_OBJ_DESCR_OBJECT(struct mcp_json_error, error, mcp_json_error_inner_descr),
};

int mcp_json_serialize_error(char *out, size_t out_len,
			     const struct mcp_request_id *id,
			     const struct mcp_error *err)
{
	if (!out || !err || out_len == 0) {
		return -EINVAL;
	}

	const char *id_str = (id && id->string[0] != '\0') ? id->string : "null";

	struct mcp_json_error json_err = {
		.jsonrpc = "2.0",
		.id = id_str,
		.error = {
			.code = err->code,
			.message = err->message,
			.data = (err->has_data && err->data_json[0] != '\0')
				? err->data_json : "null",
		},
	};

	int ret = json_obj_encode_buf(mcp_json_error_descr,
				      ARRAY_SIZE(mcp_json_error_descr),
				      &json_err, out, out_len);

	return (ret == 0) ? strlen(out) : ret;
}
