/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mcp/mcp_server.h>

LOG_MODULE_REGISTER(mcp_server, CONFIG_MCP_LOG_LEVEL);

/* Configuration defaults */

#define MCP_REQUEST_WORKERS    2
#define MCP_MESSAGE_WORKERS    2
#define MCP_REQUEST_QUEUE_SIZE 10
#define MCP_MESSAGE_QUEUE_SIZE 10
#define MCP_WORKER_PRIORITY    7

/* Message structures */
struct mcp_request_msg {
	uint32_t request_id;
	/* More fields will be added later */
};

struct mcp_message_msg {
	uint32_t token;
	/* More fields will be added later */
};

/* Message queues */
K_MSGQ_DEFINE(mcp_request_queue, sizeof(struct mcp_request_msg), MCP_REQUEST_QUEUE_SIZE, 4);
K_MSGQ_DEFINE(mcp_message_queue, sizeof(struct mcp_message_msg), MCP_MESSAGE_QUEUE_SIZE, 4);

/* Worker thread stacks */
K_THREAD_STACK_ARRAY_DEFINE(mcp_request_worker_stacks, MCP_REQUEST_WORKERS, 2048);
K_THREAD_STACK_ARRAY_DEFINE(mcp_message_worker_stacks, MCP_MESSAGE_WORKERS, 2048);

/* Worker thread structures */
static struct k_thread mcp_request_workers[MCP_REQUEST_WORKERS];
static struct k_thread mcp_message_workers[MCP_MESSAGE_WORKERS];

/* Request worker function */
static void mcp_request_worker(void *arg1, void *arg2, void *arg3)
{
	struct mcp_request_msg msg;
	int worker_id = POINTER_TO_INT(arg1);

	LOG_INF("Request worker %d started", worker_id);

	while (1) {
		if (k_msgq_get(&mcp_request_queue, &msg, K_FOREVER) == 0) {
			LOG_DBG("Processing request (worker %d)", worker_id);
		}
	}
}

/* Message worker function */
static void mcp_message_worker(void *arg1, void *arg2, void *arg3)
{
	struct mcp_message_msg msg;
	int worker_id = POINTER_TO_INT(arg1);

	LOG_INF("Message worker %d started", worker_id);

	while (1) {
		if (k_msgq_get(&mcp_message_queue, &msg, K_FOREVER) == 0) {
			LOG_DBG("Processing message (worker %d)", worker_id);
		}
	}
}

int mcp_server_init(void)
{
	LOG_INF("Initializing MCP Server Core");

	/* Initialize request worker threads */
	for (int i = 0; i < MCP_REQUEST_WORKERS; i++) {
		k_thread_create(&mcp_request_workers[i], mcp_request_worker_stacks[i],
				K_THREAD_STACK_SIZEOF(mcp_request_worker_stacks[i]),
				mcp_request_worker, INT_TO_POINTER(i), NULL, NULL,
				K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);

		k_thread_name_set(&mcp_request_workers[i], "mcp_req_worker");
	}

	/* Initialize message worker threads */
	for (int i = 0; i < MCP_MESSAGE_WORKERS; i++) {
		k_thread_create(&mcp_message_workers[i], mcp_message_worker_stacks[i],
				K_THREAD_STACK_SIZEOF(mcp_message_worker_stacks[i]),
				mcp_message_worker, INT_TO_POINTER(i), NULL, NULL,
				K_PRIO_COOP(MCP_WORKER_PRIORITY), 0, K_NO_WAIT);

		k_thread_name_set(&mcp_message_workers[i], "mcp_msg_worker");
	}

	LOG_INF("MCP Server Core initialized: %d request workers, %d message workers",
		MCP_REQUEST_WORKERS, MCP_MESSAGE_WORKERS);

	return 0;
}
