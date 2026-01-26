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
	 * Send MCP response data to a client via transport
	 *
	 * This function queues response data for delivery to a client.
	 *
	 * Data Ownership and Memory Management:
	 * -------------------------------------
	 * INPUT (data parameter):
	 *   - Caller (MCP core) allocates the response data buffer
	 *   - This function takes OWNERSHIP of the data pointer
	 *   - Data is NOT copied - the pointer is stored directly in the response item
	 *   - Caller must NOT free the data after calling this function
	 *
	 * @param binding Transport binding containing client context
	 * @param client_id Client identifier
	 * @param data Response data buffer (ownership transferred to this function)
	 * @param length Length of the response data in bytes
	 *
	 * @return 0 on success, negative error code on failure.
	 *
	 * @note The data pointer MUST remain valid until freed by the transport
	 */
	int (*send)(struct mcp_transport_binding *binding, uint32_t client_id, const void *data,
		    size_t length);

	/**
	 * @brief Disconnect a client
	 *
	 * Disconnects a client from the transport and cleans up associated resources.
	 *
	 * IMPORTANT: The transport implementation MUST drain and free any queued
	 * response data that has not yet been sent to the client. This includes:
	 * - Response items in any FIFO/message queues
	 * - The actual response data buffers (allocated by MCP core)
	 * - Any other dynamically allocated resources associated with the client
	 *
	 * Failure to properly free queued data will result in memory leaks.
	 *
	 * @param binding Client transport binding
	 * @param client_id Client identifier
	 * @return 0 on success, negative errno on failure
	 */
	int (*disconnect)(struct mcp_transport_binding *binding, uint32_t client_id);
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
typedef int (*new_client_cb)(struct mcp_transport_binding *binding, uint32_t client_id);

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
 * Handle an incoming MCP request from a client
 *
 * This function is the main entry point for processing MCP protocol requests.
 * It parses the incoming JSON request, determines the method type, and routes
 * the request to the appropriate handler or queues it for asynchronous processing.
 *
 * Request handling flow:
 * 1. Parse JSON request into MCP message structure
 * 2. Determine the method type (initialize, ping, tools_list, etc.)
 * 3. Route based on method:
 *    - INITIALIZE: Handle directly and create new client context
 *    - PING: Handle directly with immediate response
 *    - TOOLS_LIST/TOOLS_CALL/NOTIF_*: Queue for async processing
 *    - UNKNOWN: Send error response for unsupported methods
 *
 * @param ctx MCP server context handle
 * @param request Request data containing JSON payload and client hint
 * @param method Output parameter for the detected method type
 * @param client_binding Output parameter for the client's transport binding
 *
 * @return 0 on success, negative error code on failure:
 *         -EINVAL: Invalid parameters (NULL pointers)
 *         -ENOMEM: Failed to allocate memory for message parsing
 *         -ENOENT: Client not found for given client_id_hint
 *         -ENOTSUP: Request method not recognized/supported
 *         Other negative values from parsing or handler functions
 *
 * @note The parsed message is either freed immediately (for direct handlers)
 *       or ownership is transferred to the request queue (for async handlers)
 * @note The client_binding output is only valid if a client context was found
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
