#ifndef ZEPHYR_SUBSYS_NET_LIB_MCP_MCP_SERVER_INTERNAL_H_
#define ZEPHYR_SUBSYS_NET_LIB_MCP_MCP_SERVER_INTERNAL_H_

#include <zephyr/kernel.h>
#include "mcp_common.h"
/**
 * @brief Submit a parsed request from transport to MCP server (INTERNAL)
 *
 * This is an internal API used by the transport layer to forward
 * parsed requests to the server. Applications should NOT call this.
 *
 * @param json Raw serialized request data (ownership transferred to server)
 * @param length Size of the json data
 * @param in_client_id Client ID belonging to the request. 0 if new client
 * @param out_client_id The client ID used by the core is stored here
 * @param msg_type The request type is stored here
 * @return 0 on success, negative errno on failure
 */
int mcp_server_handle_request(const char *json, size_t length, uint32_t in_client_id, uint32_t *out_client_id, mcp_queue_msg_type_t *msg_type);

#endif /* ZEPHYR_SUBSYS_NET_LIB_MCP_MCP_SERVER_INTERNAL_H_ */