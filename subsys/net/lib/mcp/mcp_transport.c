/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_transport.h"

int mcp_transport_queue_response(mcp_response_queue_msg_t *msg)
{
	/* queue msg to the correct requests queue */
	return 0;
}
