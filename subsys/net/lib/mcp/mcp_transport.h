/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_TRANSPORT_H_
#define ZEPHYR_SUBSYS_MCP_TRANSPORT_H_

#include <zephyr/kernel.h>
#include "mcp_common.h"

/* Transport mechanism operations */
struct mcp_transport_ops {
	/**
	 * @brief Initialize the transport mechanism
	 * @return 0 on success, negative errno on failure
	 */
	int (*init)(void);

	/**
	 * @brief Start the transport mechanism
	 * @return 0 on success, negative errno on failure
	 */
	int (*start)(void);

	/**
	 * @brief Stop the transport mechanism
	 * @return 0 on success, negative errno on failure
	 */
	int (*stop)(void);

	/**
	 * @brief Send data to a client
	 * @param client_id Client identifier
	 * @param data Data buffer to send
	 * @param length Data length
	 * @return 0 on success, negative errno on failure
	 */
	int (*send)(uint32_t client_id, const void *data, size_t length);

	/**
	 * @brief Check if client is connected
	 * @param client_id Client identifier
	 * @return true if connected, false otherwise
	 */
	bool (*is_connected)(uint32_t client_id);

	/**
	 * @brief Get transport name
	 * @return Transport name string
	 */
	const char *(*get_name)(void);
};

/* Transport mechanism registration */
struct mcp_transport_mechanism {
	const char *name;
	const struct mcp_transport_ops *ops;
};

/**
 * @brief Register a transport mechanism
 * @param mechanism Transport mechanism to register
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_register_mechanism(const struct mcp_transport_mechanism *mechanism);

/**
 * @brief Initialize transport layer (policy)
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_init(void);

/**
 * @brief Start transport layer
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_start(void);

/**
 * @brief Queue a response for transmission
 * @param type Response message type
 * @param data Response data
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_queue_response(mcp_queue_msg_type_t type, void *data);

/**
 * @brief Send a JSON-RPC request from transport to MCP server
 *
 * This is the proper API for transport mechanisms to submit requests.
 * It handles JSON parsing and forwards the parsed request to the server.
 *
 * @param json JSON-RPC request string
 * @param length Length of JSON string
 * @param client_id Client identifier for the request
 * @return 0 on success, negative errno on failure
 *         -EINVAL if JSON parsing fails
 *         -ENOMEM if memory allocation fails
 */
int mcp_transport_send_request(const char *json, size_t length, uint32_t client_id);

/**
 * @brief Notify transport of client connection
 * @param client_id Client identifier
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_client_connected(uint32_t client_id);

/**
 * @brief Notify transport of client disconnection
 * @param client_id Client identifier
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_client_disconnected(uint32_t client_id);

/**
 * @brief Map request ID to client ID
 * @param request_id Request identifier
 * @param client_id Client identifier
 * @return 0 on success, negative errno on failure
 */
int mcp_transport_map_request_to_client(uint32_t request_id, uint32_t client_id);

/**
 * @brief Get client ID for a request ID
 * @param request_id Request identifier
 * @return client_id on success, 0 if not found
 */
uint32_t mcp_transport_get_client_for_request(uint32_t request_id);

#endif /* ZEPHYR_SUBSYS_MCP_TRANSPORT_H_ */
