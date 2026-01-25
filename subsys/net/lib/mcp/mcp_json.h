/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is tailored for an MCP *server* on Zephyr that supports:
 *   - initialize (+ notifications/initialized)
 *   - ping
 *   - tools/list
 *   - tools/call
 *   - notifications/cancelled
 *   - logging/tools notifications (outgoing only)
 *
 * Parser uses Zephyr JSON library: <zephyr/data/json.h>
 */
#ifndef ZEPHYR_SUBSYS_MCP_JSON_H_
#define ZEPHYR_SUBSYS_MCP_JSON_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tunable limits */
#define MCP_MAX_NAME_LEN       64  /* tool names, client/server names */
#define MCP_MAX_DESC_LEN       128 /* log messages, reasons, etc. */
#define MCP_MAX_TEXT_LEN       256 /* text content block */
#define MCP_MAX_PROTO_VER_LEN  32  /* "2024-11-05" etc. */
#define MCP_MAX_VERSION_LEN    32
#define MCP_MAX_JSON_CHUNK_LEN 512 /* for small opaque JSON blobs */
#define MCP_MAX_CONTENT_ITEMS  2

/* JSON-RPC message kind (incoming) */
enum mcp_msg_kind {
	MCP_MSG_INVALID = 0,
	MCP_MSG_REQUEST,
	MCP_MSG_NOTIFICATION,
};

/* Method identifiers (subset used by server) */
enum mcp_method {
	MCP_METHOD_UNKNOWN = 0,
	/* Core lifecycle */
	MCP_METHOD_INITIALIZE,
	MCP_METHOD_PING,
	/* Tools domain */
	MCP_METHOD_TOOLS_LIST,
	MCP_METHOD_TOOLS_CALL,
	/* Notifications (client to server) */
	MCP_METHOD_NOTIF_INITIALIZED,
	MCP_METHOD_NOTIF_CANCELLED,
};

/* JSON-RPC error codes (common subset) */
enum mcp_error_code {
	MCP_ERR_PARSE_ERROR = -32700,
	MCP_ERR_INVALID_REQUEST = -32600,
	MCP_ERR_METHOD_NOT_FOUND = -32601,
	MCP_ERR_INVALID_PARAMS = -32602,
	MCP_ERR_INTERNAL_ERROR = -32603,
	MCP_ERR_SERVER_GENERIC = -32000,
	MCP_ERR_CANCELLED = -32001,
	MCP_ERR_BUSY = -32002,
	MCP_ERR_NOT_INITIALIZED = -32003,
};

/* Generic JSON-RPC error object (outgoing) */
struct mcp_error {
	int32_t code;
	char message[MCP_MAX_DESC_LEN];
	char data_json[MCP_MAX_JSON_CHUNK_LEN]; /* optional; empty if !has_data */
	bool has_data;
};

/* Content type (for tool results) */
enum mcp_content_type {
	MCP_CONTENT_TEXT = 0, /* extend later with more types if needed */
};

struct mcp_content {
	enum mcp_content_type type;
	char text[MCP_MAX_TEXT_LEN]; /* if type == TEXT */
};

struct mcp_content_list {
	uint8_t count;
	struct mcp_content items[MCP_MAX_CONTENT_ITEMS];
};

/* Per-method param/result structs */
/* --- initialize --- */
struct mcp_params_initialize {
	char protocol_version[MCP_MAX_PROTO_VER_LEN];
	/* Capabilities/clientInfo fields can be added if needed */
};

struct mcp_result_initialize {
	char protocol_version[MCP_MAX_PROTO_VER_LEN];
	char server_name[MCP_MAX_NAME_LEN];
	char server_version[MCP_MAX_VERSION_LEN];
	/* Server capabilities as opaque JSON, optional. */
	char capabilities_json[MCP_MAX_JSON_CHUNK_LEN];
	bool has_capabilities;
};

/* --- ping --- */
struct mcp_params_ping {
	/* Optionally store opaque payload JSON (unused for now). */
	char payload_json[MCP_MAX_JSON_CHUNK_LEN];
	bool has_payload;
};

struct mcp_result_ping {
	char payload_json[MCP_MAX_JSON_CHUNK_LEN];
	bool has_payload;
};

/* --- tools/list --- */
struct mcp_params_tools_list {
	/* Reserved for future filters; usually empty. */
	char filter_json[MCP_MAX_JSON_CHUNK_LEN];
	bool has_filter;
};

struct mcp_result_tools_list {
	/* For now, keep the tools array as raw JSON that the user generates. */
	char tools_json[MCP_MAX_JSON_CHUNK_LEN];
};

/* --- tools/call --- */
struct mcp_params_tools_call {
	char name[MCP_MAX_NAME_LEN];                 /* tool name */
	char arguments_json[MCP_MAX_JSON_CHUNK_LEN]; /* full JSON of "arguments" object */
	bool has_arguments;
};

struct mcp_result_tools_call {
	struct mcp_content_list content; /* one or more content blocks, usually text */
};

/* --- notifications/initialized --- */
struct mcp_params_notif_initialized {
	bool dummy; /* no fields; just a marker */
};

/* --- notifications/cancelled --- */
struct mcp_params_notif_cancelled {
	int64_t request_id;
	char reason[MCP_MAX_DESC_LEN];
	bool has_reason;
};

/* Top-level incoming message struct */
struct mcp_message {
	enum mcp_msg_kind kind; /* REQUEST / NOTIFICATION */
	bool has_id;
	int64_t id;             /* only valid if has_id == true */
	enum mcp_method method; /* MCP_METHOD_* enum; UNKNOWN if not recognized */
	/* Direction-specific payload: */
	union {
		/* For REQUESTS */
		struct {
			union {
				struct mcp_params_initialize initialize;
				struct mcp_params_ping ping;
				struct mcp_params_tools_list tools_list;
				struct mcp_params_tools_call tools_call;
			} u;
		} req;
		/* For NOTIFICATIONS */
		struct {
			union {
				struct mcp_params_notif_initialized initialized;
				struct mcp_params_notif_cancelled cancelled;
			} u;
		} notif;
	};
};

/*******************************************************************************
 * Public API – parser
 ******************************************************************************/
/**
 * @brief Parse a single MCP JSON message into an mcp_message structure.
 *
 * Designed for *server-side* messages:
 *   - Requests: initialize, ping, tools/list, tools/call
 *   - Notifications: notifications/initialized, notifications/cancelled
 *
 * @param buf JSON buffer (NOT required to be NUL-terminated).
 * @param len Length of JSON in buf.
 * @param out Output message structure (must be non-NULL).
 *
 * @return 0 on success, -EINVAL on parse/validation error.
 */
int mcp_json_parse_message(const char *buf, size_t len, struct mcp_message *out);

/*******************************************************************************
 * Public API – serializers
 ******************************************************************************/
/**
 * @brief Serialize a successful initialize response.
 *
 * Generates a JSON-RPC response message:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{
 *     "protocolVersion":"...",
 *     "serverInfo":{"name":"...","version":"..."},
 *     "capabilities":{...}   // optional if has_capabilities
 *   }
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param id JSON-RPC request ID to include in the response.
 * @param res Pointer to the initialize result structure containing response data.
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_initialize_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_initialize *res);

/**
 * @brief Serialize a successful ping response.
 *
 * Generates a JSON-RPC response message:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{}
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param id JSON-RPC request ID to include in the response.
 * @param res Pointer to the ping result structure (may contain optional payload).
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_ping_result(char *out, size_t out_len, int64_t id,
				   const struct mcp_result_ping *res);

/**
 * @brief Serialize a tools/list response.
 *
 * Generates a JSON-RPC response message:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{"tools":[ ... ]}
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param id JSON-RPC request ID to include in the response.
 * @param res Pointer to the tools list result structure. The tools_json field
 *            must contain a valid JSON array, e.g.:
 *            [ {"name":"foo",...}, {"name":"bar",...} ]
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_tools_list_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_tools_list *res);

/**
 * @brief Serialize a tools/call response.
 *
 * Generates a JSON-RPC response message:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{
 *     "content":[{"type":"text","text":"..."}]
 *   }
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param id JSON-RPC request ID to include in the response.
 * @param res Pointer to the tools call result structure containing content blocks.
 *            Only content.type == TEXT is supported for now.
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_tools_call_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_tools_call *res);

/**
 * @brief Serialize a JSON-RPC error response.
 *
 * Generates a JSON-RPC error message:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id> or null,
 *   "error":{"code":X,"message":"...","data":...}
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param has_id Whether the error response should include an ID field.
 * @param id JSON-RPC request ID (only used if has_id is true).
 * @param err Pointer to the error structure containing code, message, and optional data.
 *            If err->has_data is false, the "data" field is omitted.
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_error(char *out, size_t out_len, bool has_id, int64_t id,
			     const struct mcp_error *err);

/**
 * @brief Serialize a logging message notification.
 *
 * Generates a JSON-RPC notification message:
 * {
 *   "jsonrpc":"2.0",
 *   "method":"notifications/logging/message",
 *   "params":{
 *     "level":"info",
 *     "logger":"mcp-server",
 *     "message":"...",
 *     "data":{...} // optional
 *   }
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 * @param level Log level string (e.g., "info", "warning", "error").
 * @param logger Logger name/identifier string.
 * @param message Log message content string.
 * @param data_json Optional JSON data (must be valid JSON if has_data is true).
 * @param has_data Whether to include the data field in the notification.
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_logging_message_notif(char *out, size_t out_len, const char *level,
					     const char *logger, const char *message,
					     const char *data_json, bool has_data);

/**
 * @brief Serialize a tools/list_changed notification.
 *
 * Generates a JSON-RPC notification message:
 * {
 *   "jsonrpc":"2.0",
 *   "method":"notifications/tools/list_changed",
 *   "params":{}
 * }
 *
 * @param out Output buffer for the serialized JSON string.
 * @param out_len Size of the output buffer.
 *
 * @return Number of bytes written (excluding NUL terminator) on success,
 *         negative error code on failure.
 */
int mcp_json_serialize_tools_list_changed_notif(char *out, size_t out_len);

int mcp_json_serialize_empty_response(char *out, size_t out_len, int64_t id);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MCP_JSON_H_ */
