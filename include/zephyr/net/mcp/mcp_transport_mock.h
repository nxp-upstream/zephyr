/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_MCP_TRANSPORT_MOCK_H_
#define ZEPHYR_INCLUDE_NET_MCP_TRANSPORT_MOCK_H_

#ifdef CONFIG_MCP_TRANSPORT_MOCK

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Mock transport API for MCP testing
 * @defgroup mcp_transport_mock MCP Mock Transport
 * @ingroup mcp
 * @{
 */

struct mcp_transport_binding;

/**
 * @brief Callback for new client registration (mock implementation)
 *
 * This function is called by the MCP server when a new client is created.
 * It sets up the transport binding with mock operations.
 *
 * @param binding Transport binding to initialize
 * @param client_id Newly assigned client ID
 */
void mcp_transport_mock_new_client_callback(struct mcp_transport_binding *binding,
					    uint32_t client_id);

/**
 * @brief Reset mock transport state
 *
 * Clears all tracked calls, messages, and client state.
 * Should be called between tests.
 */
void mcp_transport_mock_reset(void);

/**
 * @brief Inject error for next send operation
 *
 * @param error Error code to return (0 to disable injection)
 */
void mcp_transport_mock_inject_send_error(int error);

/**
 * @brief Inject error for next disconnect operation
 *
 * @param error Error code to return (0 to disable injection)
 */
void mcp_transport_mock_inject_disconnect_error(int error);

/**
 * @brief Get number of send calls made
 *
 * @return Number of times send was called
 */
int mcp_transport_mock_get_send_count(void);

/**
 * @brief Reset number of send calls made
 */
void mcp_transport_mock_reset_send_count(void);

/**
 * @brief Get number of disconnect calls made
 *
 * @return Number of times disconnect was called
 */
int mcp_transport_mock_get_disconnect_count(void);

/**
 * @brief Get the last client ID used in send/disconnect
 *
 * @return Last client ID
 */
uint32_t mcp_transport_mock_get_last_client_id(void);

/**
 * @brief Get the last message sent to a client
 *
 * @param client_id Client ID to query
 * @param length Pointer to store message length (can be NULL)
 * @return Pointer to message buffer, or NULL if client not found
 */
const char *mcp_transport_mock_get_last_message(uint32_t client_id, size_t *length);

/**
 * @}
 */

#endif /* CONFIG_MCP_TRANSPORT_MOCK */

#endif /* ZEPHYR_INCLUDE_NET_MCP_TRANSPORT_MOCK_H_ */