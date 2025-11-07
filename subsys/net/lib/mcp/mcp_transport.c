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
mcp_transport_queue_msg_t mcp_transport_last_queued_msg = {0};
#endif

int mcp_transport_queue_response(mcp_queue_msg_type_t type, void *msg_data)
{
	/* queue msg to the correct requests queue */
	mcp_transport_queue_msg_t msg;

	msg.type = type;

	switch (type) {
	case MCP_MSG_SYSTEM:
		/* Handle system messages */
		break;
	case MCP_MSG_NOTIFICATION:
		/* Notification is not tied to a request_id, use client_id to send directly to
		 * client, when server to client notifications are implemented (e.g. list_changed),
		 * keep msg.data as NULL
		 */
		break;
	case MCP_MSG_RESPONSE_INITIALIZE:
		mcp_initialize_response_t *init_response = (mcp_initialize_response_t *)msg_data;

		msg.data = msg_data;
		/* Locate correct k_msg_q based on init_response->request_id */
		/* Queue message to the transport layer */
		break;
	case MCP_MSG_RESPONSE_TOOLS_LIST:
		mcp_tools_list_response_t *tools_list_response =
			(mcp_tools_list_response_t *)msg_data;
		msg.data = msg_data;
		/* Locate correct k_msg_q based on tools_list_response->request_id */
		/* Queue message to the transport layer */
		break;
	case MCP_MSG_RESPONSE_TOOLS_CALL:
		mcp_tools_call_response_t *tools_call_response =
			(mcp_tools_call_response_t *)msg_data;
		msg.data = msg_data;
		/* Locate correct k_msg_q based on tools_list_response->request_id */
		/* Queue message to the transport layer */
		break;
	default:
		/* Unsupported message type */
		mcp_free(msg_data);
		return -EINVAL;
	}

#ifdef CONFIG_ZTEST
	/* Store call information for testing */
	mcp_transport_queue_call_count++;
	mcp_transport_last_queued_msg.type = type;
	mcp_transport_last_queued_msg.data = msg_data;

	/* TODO: Remove when transport queue is actually implemented! */
	mcp_free(msg_data);
#endif
	return 0;
}
