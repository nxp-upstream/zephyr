/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MCP_TRANSPORT_H_
#define ZEPHYR_SUBSYS_MCP_TRANSPORT_H_

#include <zephyr/kernel.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_common.h"

int mcp_transport_queue_response(mcp_response_queue_msg_t *msg);

#endif /* ZEPHYR_SUBSYS_MCP_TRANSPORT_H_ */
