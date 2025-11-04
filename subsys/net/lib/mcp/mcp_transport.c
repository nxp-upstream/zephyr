/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>
#include "mcp_transport.h"

#ifdef CONFIG_ZTEST
int mcp_transport_queue_call_count;
mcp_response_queue_msg_t mcp_transport_last_queued_msg = {0};
#endif

int mcp_transport_queue_response(mcp_response_queue_msg_t *msg)
{
#ifdef CONFIG_ZTEST
	/* Store call information for testing */
	mcp_transport_queue_call_count++;
	if (msg) {
		mcp_transport_last_queued_msg.type = msg->type;
		mcp_transport_last_queued_msg.data = msg->data;
	}
#endif

	/* queue msg to the correct requests queue */
	return 0;
}
