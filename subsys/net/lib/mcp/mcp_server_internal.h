/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_SERVER_INTERNAL_H_
#define ZEPHYR_SUBSYS_MCP_SERVER_INTERNAL_H_

/**
 * @file
 * @brief Model Context Protocol (MCP) Server Internal API
 */

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_common.h"

#define INVALID_CLIENT_ID 0

/* Forward declaration */
struct mcp_transport_binding;

/**
 * @brief Transport operations structure for MCP server communication.
 */
struct mcp_transport_ops {
	/**
	 * @brief Send data to a client
	 * @param ep Transport endpoint
	 * @param client_id Client identifier
	 * @param data Data buffer to send
	 * @param length Data length
	 * @return 0 on success, negative errno on failure
	 */
	int (*send)(struct mcp_transport_binding *ep, uint32_t client_id, const void *data,
		    size_t length);

	/**
	 * @brief Disconnect a client
	 * @param ep Transport endpoint
	 * @param client_id Client identifier
	 * @return 0 on success, negative errno on failure
	 */
	int (*disconnect)(struct mcp_transport_binding *ep, uint32_t client_id);
};

/**
 * @brief MCP endpoint structure for managing server communication
 * @details Contains transport operations and endpoint-specific context
 * for handling client connections and message delivery.
 */
struct mcp_transport_binding {
	const struct mcp_transport_ops *ops;
	void *context;
};

/**
 * @brief Callback function type for new client initialization
 *
 * This callback is invoked when a new client is created, allowing the transport
 * layer to set up the transport binding with appropriate operations and context.
 *
 * @param binding Pointer to the transport binding to be initialized
 * @param client_id The newly assigned client identifier
 */
typedef void (*new_client_cb)(struct mcp_transport_binding *binding, uint32_t client_id);

/**
 * @brief Request data structure for submitting requests to the MCP server
 *
 * This structure encapsulates all information needed to process an incoming
 * request from a transport layer, including the JSON payload, client identification,
 * and callback for new client setup.
 */
struct mcp_request_data {
	char *json_data;         /* Pointer to JSON request payload */
	size_t json_len;         /* Length of JSON data in bytes */
	uint32_t client_id_hint; /* Client ID hint (0 for new clients) */
	new_client_cb callback;  /* Callback for new client initialization */
};

/**
 * @brief Submit a parsed request from transport to MCP server (INTERNAL)
 *
 * This is an internal API used by the transport layer to forward
 * parsed requests to the server core. Applications should NOT call this directly.
 *
 * @param ctx Server context
 * @param request Request data structure containing JSON payload, length, client ID hint,
 *                and new client callback
 * @param method Pointer to store the request method type (output parameter)
 * @param client_binding Pointer to store the client's transport binding (output parameter)
 * @return MCP_HANDLE_NEW_CLIENT for new client initialization,
 *         MCP_HANDLE_OK for successful existing client request,
 *         MCP_HANDLE_ERROR on failure
 */
int mcp_server_handle_request(mcp_server_ctx_t ctx, struct mcp_request_data *request,
			      enum mcp_method *method,
			      struct mcp_transport_binding **client_binding);

/**
 * @brief Get the transport binding for a specific client (INTERNAL)
 *
 * This is an internal API used by the transport layer to retrieve
 * the transport binding associated with a client ID.
 *
 * @param ctx Server context
 * @param client_id Client identifier
 * @return Pointer to the client's transport binding, or NULL if client not found
 */
struct mcp_transport_binding *mcp_server_get_client_binding(mcp_server_ctx_t ctx,
							    uint32_t client_id);

#endif /* ZEPHYR_SUBSYS_MCP_SERVER_INTERNAL_H_ */
