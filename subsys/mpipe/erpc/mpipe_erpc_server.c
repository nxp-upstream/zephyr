/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/erpc/mpipe_erpc.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>

#include "mpipe_erpc_objects.h"

LOG_MODULE_REGISTER(mpipe_erpc_server, CONFIG_MPIPE_LOG_LEVEL);

/* The values mpipe_stream.erpc gives for the answers of a served source */
BUILD_ASSERT(EAGAIN == 11);
BUILD_ASSERT(ENODATA == 61);

/* An eRPC server and the pipeline servers it answers with */
struct mpipe_erpc_server {
	erpc_server_t erpc_server;
	struct mpipe_pipeline_server *const *pipelines;
	uint32_t num_pipelines;
};

struct mpipe_pipeline_server *mpipe_erpc_server_get_pipeline(struct mpipe_erpc_server *server,
							     uint32_t instance)
{
	if (instance >= server->num_pipelines) {
		return NULL;
	}

	return server->pipelines[instance];
}

/* Function n of the endpoint is pipeline n, and so is the instance */
static int32_t mpipe_erpc_server_open(void *ctx, uint32_t function, uint32_t *instance)
{
	*instance = function;

	return (mpipe_erpc_server_get_pipeline(ctx, function) != NULL) ? 0 : -ENOENT;
}

static int32_t mpipe_erpc_server_process(void *ctx, uint32_t instance, uint32_t in_buf,
					 uint32_t in_sz, uint32_t out_buf, uint32_t out_cap,
					 uint32_t *out_sz)
{
	struct mpipe_pipeline_server *pipeline = mpipe_erpc_server_get_pipeline(ctx, instance);

	*out_sz = 0U;

	if (pipeline == NULL) {
		return -EINVAL;
	}

	/* A source client gives no input buffer: it takes what the pipeline produces */
	if (in_buf == 0U) {
		return mpipe_pipeline_server_produce(
			pipeline, (void *)(uintptr_t)out_buf, out_cap, out_sz,
			K_MSEC(CONFIG_MPIPE_ERPC_SERVER_PRODUCE_TIMEOUT_MS));
	}

	/* A sink client gives no output buffer */
	if (out_buf == 0U) {
		return mpipe_pipeline_server_process(pipeline, (void *)(uintptr_t)in_buf, in_sz,
						     NULL, 0U, NULL);
	}

	return mpipe_pipeline_server_process(pipeline, (void *)(uintptr_t)in_buf, in_sz,
					     (void *)(uintptr_t)out_buf, out_cap, out_sz);
}

static int32_t mpipe_erpc_server_set_state(void *ctx, uint32_t instance, uint8_t transition)
{
	struct mpipe_pipeline_server *pipeline = mpipe_erpc_server_get_pipeline(ctx, instance);

	if (pipeline == NULL) {
		return -EINVAL;
	}

	return mpipe_pipeline_server_set_state(pipeline, MPIPE_STATE_TRANSITION_NEXT(transition));
}

static int32_t mpipe_erpc_server_eos(void *ctx, uint32_t instance)
{
	struct mpipe_pipeline_server *pipeline = mpipe_erpc_server_get_pipeline(ctx, instance);

	if (pipeline == NULL) {
		return -EINVAL;
	}

	return mpipe_pipeline_server_eos(pipeline);
}

struct mpipe_erpc_server *mpipe_erpc_server_create(struct mpipe_pipeline_server *const *pipelines,
						   uint32_t num_pipelines, const char *ept_name)
{
	static const struct mpipe_erpc_objects_stream_handler handler = {
		.open = mpipe_erpc_server_open,
		.process = mpipe_erpc_server_process,
		.set_state = mpipe_erpc_server_set_state,
		.eos = mpipe_erpc_server_eos,
	};
	static struct mpipe_erpc_server servers[CONFIG_MPIPE_ERPC_SERVERS_NUM];
	static atomic_t used;
	struct mpipe_erpc_server *server;
	atomic_val_t index;

	__ASSERT_NO_MSG(pipelines != NULL && num_pipelines > 0U);
	__ASSERT_NO_MSG(ept_name != NULL);

	index = atomic_inc(&used);
	if (index >= ARRAY_SIZE(servers)) {
		LOG_ERR("No eRPC server left for %s, see CONFIG_MPIPE_ERPC_SERVERS_NUM", ept_name);
		return NULL;
	}

	server = &servers[index];
	server->pipelines = pipelines;
	server->num_pipelines = num_pipelines;

	/* Returns once the client on the other core has bound the endpoint */
	server->erpc_server = mpipe_erpc_objects_server_create(ept_name, &handler, server);
	if (server->erpc_server == NULL) {
		LOG_ERR("Failed to bring up the eRPC server on %s", ept_name);
		return NULL;
	}

	return server;
}

void mpipe_erpc_server_add_service(struct mpipe_erpc_server *server, void *service)
{
	__ASSERT_NO_MSG(server != NULL);
	__ASSERT_NO_MSG(service != NULL);

	mpipe_erpc_objects_server_add_service(server->erpc_server, service);
}

int mpipe_erpc_server_run(struct mpipe_erpc_server *server)
{
	erpc_status_t status;

	__ASSERT_NO_MSG(server != NULL);

	/* The thread sleeps between two calls: the pipeline threads need the core */
	status = mpipe_erpc_objects_server_run(server->erpc_server);

	LOG_ERR("eRPC error %d, no longer serving", (int)status);

	return -EIO;
}
