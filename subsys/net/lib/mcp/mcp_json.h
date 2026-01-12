/* mcp_json.h - MCP JSON-RPC protocol structs + parser/serializer API
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
#ifndef ZEPHYR_INCLUDE_NET_MCP_JSON_H_
#define ZEPHYR_INCLUDE_NET_MCP_JSON_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Tunable limits ===== */
#define MCP_MAX_NAME_LEN       64  /* tool names, client/server names */
#define MCP_MAX_DESC_LEN       128 /* log messages, reasons, etc. */
#define MCP_MAX_TEXT_LEN       256 /* text content block */
#define MCP_MAX_PROTO_VER_LEN  32  /* "2024-11-05" etc. */
#define MCP_MAX_VERSION_LEN    32
#define MCP_MAX_JSON_CHUNK_LEN 512 /* for small opaque JSON blobs */

/* ===== JSON-RPC message kind (incoming) ===== */
enum mcp_msg_kind {
	MCP_MSG_INVALID = 0,
	MCP_MSG_REQUEST,
	MCP_MSG_NOTIFICATION,
};

/* ===== Method identifiers (subset used by server) ===== */
enum mcp_method {
	MCP_METHOD_UNKNOWN = 0,
	/* Core lifecycle */
	MCP_METHOD_INITIALIZE, // "initialize"
	MCP_METHOD_PING,       // "ping"
	/* Tools domain */
	MCP_METHOD_TOOLS_LIST, // "tools/list"
	MCP_METHOD_TOOLS_CALL, // "tools/call"
	/* Notifications (client → server) */
	MCP_METHOD_NOTIF_INITIALIZED, // "notifications/initialized"
	MCP_METHOD_NOTIF_CANCELLED,   // "notifications/cancelled"
};

/* ===== JSON-RPC error codes (common subset) ===== */
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

/* ===== Generic JSON-RPC error object (outgoing) ===== */
struct mcp_error {
	int32_t code;
	char message[MCP_MAX_DESC_LEN];
	char data_json[MCP_MAX_JSON_CHUNK_LEN]; /* optional; empty if !has_data */
	bool has_data;
};

/* ===== Content type (for tool results) ===== */
enum mcp_content_type {
	MCP_CONTENT_TEXT = 0, /* extend later with more types if needed */
};

struct mcp_content {
	enum mcp_content_type type;
	char text[MCP_MAX_TEXT_LEN]; /* if type == TEXT */
};

#define MCP_MAX_CONTENT_ITEMS 2

struct mcp_content_list {
	uint8_t count;
	struct mcp_content items[MCP_MAX_CONTENT_ITEMS];
};

/* ===== Per-method param/result structs ===== */
/* --- initialize --- */
struct mcp_params_initialize {
	char protocol_version[MCP_MAX_PROTO_VER_LEN];
	/* You can add capabilities/clientInfo fields later if you want. */
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
	/* For now, we keep the tools array as raw JSON that you generate. */
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

/* ===== Top-level incoming message struct ===== */
struct mcp_message {
	enum mcp_msg_kind kind; /* REQUEST / NOTIFICATION */
	bool has_id;
	int64_t id;             /* only valid if has_id == true */
	enum mcp_method method; /* MCP_METHOD_* enum; UNKNOWN if not recognized */
	/* Direction-specific payload: */
	union {
		/* === For REQUESTS === */
		struct {
			union {
				struct mcp_params_initialize initialize;
				struct mcp_params_ping ping;
				struct mcp_params_tools_list tools_list;
				struct mcp_params_tools_call tools_call;
			} u;
		} req;
		/* === For NOTIFICATIONS === */
		struct {
			union {
				struct mcp_params_notif_initialized initialized;
				struct mcp_params_notif_cancelled cancelled;
			} u;
		} notif;
	};
};

/* ============================================================
 * Public API – parser
 * ============================================================ */
/**
 * Parse a single MCP JSON message into an mcp_message_t.
 *
 * Designed for *server-side* messages:
 *   - Requests: initialize, ping, tools/list, tools/call
 *   - Notifications: notifications/initialized, notifications/cancelled
 *
 * @param buf JSON buffer (NOT required to be NUL-terminated).
 * @param len Length of JSON in buf.
 * @param out Output message structure (must be non-NULL).
 *
 * @return 0 on success,
 *        -EINVAL on parse/validation error.
 */
int mcp_json_parse_message(const char *buf, size_t len, struct mcp_message *out);

/* ============================================================
 * Public API – serializers (server → client)
 * ============================================================ */
/**
 * Serialize a successful initialize response.
 *
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{
 *     "protocolVersion":"...",
 *     "serverInfo":{"name":"...","version":"..."},
 *     "capabilities":{...}   // optional if has_capabilities
 *   }
 * }
 */
int mcp_json_serialize_initialize_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_initialize *res);

/**
 * Serialize a successful ping response:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{}
 * }
 */
int mcp_json_serialize_ping_result(char *out, size_t out_len, int64_t id,
				   const struct mcp_result_ping *res);

/**
 * Serialize a tools/list response:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{"tools":[ ... ]}
 * }
 *
 * tools_json must be a valid JSON array, e.g.:
 *   [ {"name":"foo",...}, {"name":"bar",...} ]
 */
int mcp_json_serialize_tools_list_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_tools_list *res);

/**
 * Serialize a tools/call response:
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id>,
 *   "result":{
 *     "content":[{"type":"text","text":"..."}]
 *   }
 * }
 *
 * Only content.type == TEXT is supported for now.
 */
int mcp_json_serialize_tools_call_result(char *out, size_t out_len, int64_t id,
					 const struct mcp_result_tools_call *res);

/**
 * Serialize a JSON-RPC error:
 *
 * {
 *   "jsonrpc":"2.0",
 *   "id":<id> or null,
 *   "error":{"code":X,"message":"...","data":...}
 * }
 *
 * If has_data == false, "data" is omitted.
 */
int mcp_json_serialize_error(char *out, size_t out_len, bool has_id, int64_t id,
			     const struct mcp_error *err);

/**
 * Serialize a logging message notification:
 *
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
 * level, logger, message are plain strings.
 * data_json must be valid JSON if has_data == true.
 */
int mcp_json_serialize_logging_message_notif(char *out, size_t out_len, const char *level,
					     const char *logger, const char *message,
					     const char *data_json, bool has_data);

/**
 * Serialize tools/list_changed notification:
 *
 * {
 *   "jsonrpc":"2.0",
 *   "method":"notifications/tools/list_changed",
 *   "params":{}
 * }
 */
int mcp_json_serialize_tools_list_changed_notif(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_NET_LIB_MCP_MCP_JSON_H_ */
