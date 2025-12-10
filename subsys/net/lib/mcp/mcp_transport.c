/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "mcp_server_internal.h"
#include "mcp_transport.h"
#include "mcp_json.h"

LOG_MODULE_REGISTER(mcp_transport, CONFIG_MCP_LOG_LEVEL);

#define MCP_TRANSPORT_WORKER_PRIORITY 7
#define MCP_TRANSPORT_BUFFER_SIZE 2048
#define MCP_MAX_REQUEST_MAPPINGS (CONFIG_HTTP_SERVER_MAX_CLIENTS * CONFIG_HTTP_SERVER_MAX_STREAMS)

/* Transport response queue owned by transport layer */
K_MSGQ_DEFINE(mcp_transport_queue, sizeof(mcp_transport_queue_msg_t),
		  CONFIG_MCP_RESPONSE_QUEUE_SIZE, 4);

/* Transport worker thread */
K_THREAD_STACK_DEFINE(mcp_transport_worker_stack, 2048);
static struct k_thread mcp_transport_worker;

/* Active transport mechanism */
static const struct mcp_transport_mechanism *active_mechanism;
static struct k_mutex transport_mutex;

/* Request-to-client mapping */
struct request_client_mapping {
	uint32_t request_id;
	uint32_t client_id;
	int64_t timestamp;
	bool in_use;
};

static struct {
	struct request_client_mapping mappings[MCP_MAX_REQUEST_MAPPINGS];
	struct k_mutex mutex;
} request_map;

/* Response buffer pool */
struct transport_buffer {
	uint8_t data[MCP_TRANSPORT_BUFFER_SIZE];
	size_t length;
	uint32_t client_id;
	bool in_use;
};

#define TRANSPORT_BUFFER_POOL_SIZE 4
static struct transport_buffer buffer_pool[TRANSPORT_BUFFER_POOL_SIZE];
static struct k_mutex buffer_pool_mutex;

#ifdef CONFIG_ZTEST
/* Test instrumentation - only compiled for tests */
int mcp_transport_queue_call_count = 0;
mcp_transport_queue_msg_t mcp_transport_last_queued_msg = {0};
#endif

/* Request-to-client mapping functions */

int mcp_transport_map_request_to_client(uint32_t request_id, uint32_t client_id)
{
	int ret;
	int slot = -1;

	ret = k_mutex_lock(&request_map.mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock request map mutex: %d", ret);
		return ret;
	}

	/* Find available slot */
	for (int i = 0; i < MCP_MAX_REQUEST_MAPPINGS; i++) {
		if (!request_map.mappings[i].in_use) {
			slot = i;
			break;
		}
	}

	if (slot == -1) {
		/* Try to reuse oldest entry */
		int64_t oldest_time = INT64_MAX;
		for (int i = 0; i < MCP_MAX_REQUEST_MAPPINGS; i++) {
			if (request_map.mappings[i].timestamp < oldest_time) {
				oldest_time = request_map.mappings[i].timestamp;
				slot = i;
			}
		}
		LOG_WRN("Request map full, reusing slot %d", slot);
	}

	request_map.mappings[slot].request_id = request_id;
	request_map.mappings[slot].client_id = client_id;
	request_map.mappings[slot].timestamp = k_uptime_get();
	request_map.mappings[slot].in_use = true;

	k_mutex_unlock(&request_map.mutex);

	LOG_DBG("Mapped request %u to client %u", request_id, client_id);
	return 0;
}

uint32_t mcp_transport_get_client_for_request(uint32_t request_id)
{
	uint32_t client_id = 0;
	int ret;

	ret = k_mutex_lock(&request_map.mutex, K_MSEC(100));
	if (ret != 0) {
		LOG_ERR("Failed to lock request map mutex: %d", ret);
		return 0;
	}

	for (int i = 0; i < MCP_MAX_REQUEST_MAPPINGS; i++) {
		if (request_map.mappings[i].in_use &&
			request_map.mappings[i].request_id == request_id) {
			client_id = request_map.mappings[i].client_id;
			/* Clear mapping after use */
			request_map.mappings[i].in_use = false;
			break;
		}
	}

	k_mutex_unlock(&request_map.mutex);

	if (client_id == 0) {
		LOG_WRN("No client mapping found for request %u", request_id);
	}

	return client_id;
}

/* Buffer management */
static struct transport_buffer *acquire_buffer(void)
{
	struct transport_buffer *buf = NULL;
	int ret;

	ret = k_mutex_lock(&buffer_pool_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock buffer pool mutex: %d", ret);
		return NULL;
	}

	for (int i = 0; i < TRANSPORT_BUFFER_POOL_SIZE; i++) {
		if (!buffer_pool[i].in_use) {
			buffer_pool[i].in_use = true;
			buf = &buffer_pool[i];
			break;
		}
	}

	k_mutex_unlock(&buffer_pool_mutex);

	if (!buf) {
		LOG_WRN("No available transport buffers");
	}

	return buf;
}

static void release_buffer(struct transport_buffer *buf)
{
	int ret;

	if (!buf) {
		return;
	}

	ret = k_mutex_lock(&buffer_pool_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock buffer pool mutex: %d", ret);
		return;
	}

	buf->in_use = false;
	buf->length = 0;
	buf->client_id = 0;

	k_mutex_unlock(&buffer_pool_mutex);
}

/* Extract client_id from response data based on message type */
static uint32_t extract_client_id(mcp_queue_msg_type_t type, void *data)
{
	uint32_t request_id = 0;
	uint32_t client_id = 0;

	/* Extract request_id from response structure */
	switch (type) {
	case MCP_MSG_RESPONSE_INITIALIZE: {
		mcp_initialize_response_t *resp = (mcp_initialize_response_t *)data;
		request_id = resp->request_id;
		break;
	}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_MSG_RESPONSE_TOOLS_LIST: {
		mcp_tools_list_response_t *resp = (mcp_tools_list_response_t *)data;
		request_id = resp->request_id;
		break;
	}

	case MCP_MSG_RESPONSE_TOOLS_CALL: {
		mcp_tools_call_response_t *resp = (mcp_tools_call_response_t *)data;
		request_id = resp->request_id;
		break;
	}
#endif

	case MCP_MSG_ERROR_INITIALIZE:
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_MSG_ERROR_TOOLS_LIST:
	case MCP_MSG_ERROR_TOOLS_CALL:
#endif
	{
		mcp_error_response_t *resp = (mcp_error_response_t *)data;
		request_id = resp->request_id;
		break;
	}

	default:
		LOG_WRN("Unknown response type: %d", type);
		return 0;
	}

	/* Look up client_id from request_id */
	if (request_id != 0) {
		client_id = mcp_transport_get_client_for_request(request_id);
	}

	return client_id;
}

/* Serialize response to JSON */
static int serialize_response(mcp_queue_msg_type_t type, void *data,
				  struct transport_buffer *buf)
{
	int ret;

	switch (type) {
	case MCP_MSG_RESPONSE_INITIALIZE: {
		mcp_initialize_response_t *resp = (mcp_initialize_response_t *)data;
		ret = mcp_json_serialize_initialize_response(resp, (char *)buf->data,
								 MCP_TRANSPORT_BUFFER_SIZE);
		break;
	}

#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_MSG_RESPONSE_TOOLS_LIST: {
		mcp_tools_list_response_t *resp = (mcp_tools_list_response_t *)data;
		ret = mcp_json_serialize_tools_list_response(resp, (char *)buf->data,
								 MCP_TRANSPORT_BUFFER_SIZE);
		break;
	}

	case MCP_MSG_RESPONSE_TOOLS_CALL: {
		mcp_tools_call_response_t *resp = (mcp_tools_call_response_t *)data;
		ret = mcp_json_serialize_tools_call_response(resp, (char *)buf->data,
								 MCP_TRANSPORT_BUFFER_SIZE);
		break;
	}
#endif

	case MCP_MSG_ERROR_INITIALIZE:
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_MSG_ERROR_TOOLS_LIST:
	case MCP_MSG_ERROR_TOOLS_CALL:
#endif
	{
		mcp_error_response_t *resp = (mcp_error_response_t *)data;
		ret = mcp_json_serialize_error_response(resp, (char *)buf->data,
								MCP_TRANSPORT_BUFFER_SIZE);
		break;
	}

	default:
		LOG_ERR("Unknown response type: %d", type);
		return -EINVAL;
	}

	if (ret < 0) {
		LOG_ERR("Serialization failed: %d", ret);
		return ret;
	}

	buf->length = ret;
	return 0;
}

/* Extract request_id from parsed message data */
static uint32_t extract_request_id(mcp_queue_msg_type_t type, void *data)
{
	uint32_t request_id = 0;

	switch (type) {
	case MCP_MSG_REQUEST_INITIALIZE:
		request_id = ((mcp_initialize_request_t *)data)->request_id;
		break;
#ifdef CONFIG_MCP_TOOLS_CAPABILITY
	case MCP_MSG_REQUEST_TOOLS_LIST:
		request_id = ((mcp_tools_list_request_t *)data)->request_id;
		break;
	case MCP_MSG_REQUEST_TOOLS_CALL:
		request_id = ((mcp_tools_call_request_t *)data)->request_id;
		break;
#endif
	case MCP_MSG_NOTIFICATION:
		/* Notifications don't have request IDs */
		break;
	default:
		LOG_WRN("Unknown request type: %d", type);
		break;
	}

	return request_id;
}

/* Transport worker thread */
static void mcp_transport_worker_fn(void *arg1, void *arg2, void *arg3)
{
	mcp_transport_queue_msg_t msg;
	struct transport_buffer *buf;
	int ret;

	LOG_INF("Transport worker started");

	while (1) {
		ret = k_msgq_get(&mcp_transport_queue, &msg, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to get transport message: %d", ret);
			continue;
		}

		if (!msg.data) {
			LOG_ERR("NULL data in transport message");
			continue;
		}

		/* Acquire buffer for serialization */
		buf = acquire_buffer();
		if (!buf) {
			LOG_ERR("No available buffers, dropping message");
			mcp_free(msg.data);
			continue;
		}

		/* Extract client ID from response */
		buf->client_id = extract_client_id(msg.type, msg.data);

		/* Serialize response to JSON */
		ret = serialize_response(msg.type, msg.data, buf);
		if (ret != 0) {
			LOG_ERR("Failed to serialize response: %d", ret);
			release_buffer(buf);
			mcp_free(msg.data);
			continue;
		}

		/* Free original data structure */
		mcp_free(msg.data);

		/* Send via active transport mechanism */
		ret = k_mutex_lock(&transport_mutex, K_FOREVER);
		if (ret != 0) {
			LOG_ERR("Failed to lock transport mutex: %d", ret);
			release_buffer(buf);
			continue;
		}

		if (active_mechanism && active_mechanism->ops->send) {
			ret = active_mechanism->ops->send(buf->client_id, buf->data,
							  buf->length);
			if (ret != 0) {
				LOG_ERR("Transport send failed: %d", ret);
			} else {
				LOG_DBG("Sent response to client %u (%zu bytes)",
					buf->client_id, buf->length);
			}
		} else {
			LOG_ERR("No active transport mechanism");
		}

		k_mutex_unlock(&transport_mutex);
		release_buffer(buf);
	}
}

int mcp_transport_register_mechanism(const struct mcp_transport_mechanism *mechanism)
{
	int ret;

	if (!mechanism || !mechanism->ops) {
		LOG_ERR("Invalid transport mechanism");
		return -EINVAL;
	}

	ret = k_mutex_lock(&transport_mutex, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to lock transport mutex: %d", ret);
		return ret;
	}

	if (active_mechanism) {
		LOG_WRN("Replacing existing transport mechanism");
	}

	active_mechanism = mechanism;
	LOG_INF("Registered transport mechanism: %s", mechanism->name);

	k_mutex_unlock(&transport_mutex);
	return 0;
}

int mcp_transport_init(void)
{
	int ret;

	LOG_INF("Initializing MCP transport layer");

	ret = k_mutex_init(&transport_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init transport mutex: %d", ret);
		return ret;
	}

	ret = k_mutex_init(&buffer_pool_mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init buffer pool mutex: %d", ret);
		return ret;
	}

	ret = k_mutex_init(&request_map.mutex);
	if (ret != 0) {
		LOG_ERR("Failed to init request map mutex: %d", ret);
		return ret;
	}

	/* Initialize buffer pool */
	memset(buffer_pool, 0, sizeof(buffer_pool));

	/* Initialize request mapping */
	memset(&request_map.mappings, 0, sizeof(request_map.mappings));

	/* Initialize active mechanism if registered */
	if (active_mechanism && active_mechanism->ops->init) {
		ret = active_mechanism->ops->init();
		if (ret != 0) {
			LOG_ERR("Transport mechanism init failed: %d", ret);
			return ret;
		}
	}

	LOG_INF("MCP transport layer initialized");
	return 0;
}

int mcp_transport_start(void)
{
	k_tid_t tid;
	int ret;

	LOG_INF("Starting MCP transport layer");

	/* Start transport worker */
	tid = k_thread_create(&mcp_transport_worker, mcp_transport_worker_stack,
				  K_THREAD_STACK_SIZEOF(mcp_transport_worker_stack),
				  mcp_transport_worker_fn, NULL, NULL, NULL,
				  K_PRIO_COOP(MCP_TRANSPORT_WORKER_PRIORITY), 0, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("Failed to create transport worker");
		return -ENOMEM;
	}

	ret = k_thread_name_set(&mcp_transport_worker, "mcp_transport");
	if (ret != 0) {
		LOG_WRN("Failed to set thread name: %d", ret);
	}

	/* Start active mechanism */
	if (active_mechanism && active_mechanism->ops->start) {
		ret = active_mechanism->ops->start();
		if (ret != 0) {
			LOG_ERR("Transport mechanism start failed: %d", ret);
			return ret;
		}
	}

	LOG_INF("MCP transport layer started");
	return 0;
}

int mcp_transport_queue_response(mcp_queue_msg_type_t type, void *data)
{
	mcp_transport_queue_msg_t msg;
	int ret;

	if (!data) {
		LOG_ERR("NULL data in response");
		return -EINVAL;
	}

	msg.type = type;
	msg.data = data;

#ifdef CONFIG_ZTEST
	/* Store call information for testing */
	mcp_transport_queue_call_count++;
	mcp_transport_last_queued_msg.type = type;
	mcp_transport_last_queued_msg.data = data;
#endif

	ret = k_msgq_put(&mcp_transport_queue, &msg, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("Failed to queue response: %d", ret);
		return ret;
	}

	return 0;
}

int mcp_transport_send_request(const char *json, size_t length, uint32_t client_id)
{
	mcp_queue_msg_type_t msg_type;
	void *msg_data = NULL;
	uint32_t request_id;
	int ret;

	if (!json || length == 0) {
		LOG_ERR("Invalid request parameters");
		return -EINVAL;
	}

	LOG_DBG("Transport parsing JSON request from client %u (%zu bytes)", client_id, length);

	/* Parse JSON request */
	ret = mcp_json_parse_request(json, length, client_id, &msg_type, &msg_data);
	if (ret != 0) {
		LOG_ERR("Failed to parse JSON request: %d", ret);
		return -EINVAL;
	}

	if (!msg_data) {
		LOG_ERR("JSON parsing returned NULL data");
		return -EINVAL;
	}

	/* Extract request_id and map to client_id */
	request_id = extract_request_id(msg_type, msg_data);
	if (request_id != 0) {
		ret = mcp_transport_map_request_to_client(request_id, client_id);
		if (ret != 0) {
			LOG_WRN("Failed to map request to client: %d", ret);
			/* Continue anyway - mapping is for response routing */
		}
	}

	/* Forward to MCP server via its public API */
	ret = mcp_server_submit_request(msg_type, msg_data);
	if (ret != 0) {
		LOG_ERR("Failed to submit request to server: %d", ret);
		mcp_free(msg_data);
		return ret;
	}

	LOG_DBG("Request forwarded to server (type=%d, request_id=%u)", msg_type, request_id);
	return 0;
}

int mcp_transport_client_connected(uint32_t client_id)
{
	LOG_INF("Client %u connected", client_id);
	return 0;
}

int mcp_transport_client_disconnected(uint32_t client_id)
{
	mcp_system_msg_t *system_msg;
	int ret;

	LOG_INF("Client %u disconnected", client_id);

	/* Clean up any pending request mappings for this client */
	ret = k_mutex_lock(&request_map.mutex, K_FOREVER);
	if (ret == 0) {
		for (int i = 0; i < MCP_MAX_REQUEST_MAPPINGS; i++) {
			if (request_map.mappings[i].in_use &&
				request_map.mappings[i].client_id == client_id) {
				request_map.mappings[i].in_use = false;
			}
		}
		k_mutex_unlock(&request_map.mutex);
	}

	/* Notify MCP server of client shutdown */
	system_msg = (mcp_system_msg_t *)mcp_alloc(sizeof(mcp_system_msg_t));
	if (!system_msg) {
		LOG_ERR("Failed to allocate system message");
		return -ENOMEM;
	}

	system_msg->type = MCP_SYS_CLIENT_SHUTDOWN;
	system_msg->client_id = client_id;
	system_msg->request_id = 0;

	ret = mcp_server_submit_request(MCP_MSG_SYSTEM, system_msg);
	if (ret != 0) {
		LOG_ERR("Failed to submit shutdown message: %d", ret);
		mcp_free(system_msg);
		return ret;
	}

	return 0;
}
