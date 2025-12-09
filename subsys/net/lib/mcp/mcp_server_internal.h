/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_INTERNAL_H_
#define ZEPHYR_SUBSYS_MCP_INTERNAL_H_

/**
 * @file
 * @brief MCP Internal APIs - NOT FOR APPLICATION USE
 *
 * This header contains internal APIs used between MCP subsystem components.
 * Applications should NOT include this header.
 *
 * @warning INTERNAL USE ONLY - This is not a public API
 */

#include "mcp_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Submit a parsed request from transport to MCP server (INTERNAL)
 *
 * This is an internal API used by the transport layer to forward
 * parsed requests to the server. Applications should NOT call this.
 *
 * @param type Request message type
 * @param data Request data (ownership transferred to server)
 * @return 0 on success, negative errno on failure
 */
int mcp_server_submit_request(mcp_queue_msg_type_t type, void *data);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MCP_INTERNAL_H_ */
