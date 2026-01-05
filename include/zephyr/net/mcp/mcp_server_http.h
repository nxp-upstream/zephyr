#ifndef ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_
#define ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_

#include <zephyr/kernel.h>

int mcp_server_http_init(void);
int mcp_server_http_send(uint32_t client_id, const void *data, size_t length);
int mcp_server_http_disconnect(uint32_t client_id);

#define MCP_SERVER_HTTP_DT_DEFINE(_name) \
	struct mcp_transport_ops _name = {\
		.init = mcp_server_http_init,\
		.send = mcp_server_http_send,\
		.disconnect = mcp_server_http_disconnect,\
	};

#endif /* ZEPHYR_INCLUDE_NET_MCP_MCP_SERVER_HTTP_H_ */
