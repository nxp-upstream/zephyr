/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>

LOG_MODULE_REGISTER(mpipe_pipeline_server, CONFIG_MPIPE_LOG_LEVEL);

/* The pad the application source feeds: the first served element's sink pad */
static inline struct mpipe_pad *server_entry_pad(struct mpipe_pipeline_server *server)
{
	return server->in->src.src_pad.peer;
}

/* The pad feeding the application sink: the last served element's src pad */
static inline struct mpipe_pad *server_exit_pad(struct mpipe_pipeline_server *server)
{
	return server->out->sink.sink_pad.peer;
}

static inline struct mpipe_element *server_pipeline_element(struct mpipe_pipeline_server *server)
{
	return (struct mpipe_element *)server->pipeline;
}

/* Wrap memory of the client in a buffer owned by one of the server's pools */
static struct net_buf *server_wrap(struct mpipe_buffer_pool *pool, void *data, uint32_t size,
				   uint32_t used)
{
	struct mpipe_buffer_meta *meta;
	struct net_buf *buf;

	buf = net_buf_alloc_with_data(pool->nb_pool, data, size, K_NO_WAIT);
	if (buf == NULL) {
		return NULL;
	}

	meta = mpipe_buffer_get_meta(buf);
	meta->pool = pool;
	meta->bytes_used = used;
	meta->pts = 0;
	meta->driver_buf = NULL;
	meta->priv = NULL;
	meta->parent = NULL;
	buf->len = (uint16_t)used;

	return buf;
}

/* Hand out the client's output memory, once per call */
static int server_out_pool_acquire(struct mpipe_buffer_pool *pool, struct net_buf **buf)
{
	struct mpipe_pipeline_server *server =
		CONTAINER_OF(pool, struct mpipe_pipeline_server, out_pool);

	if (server->out_buf == NULL) {
		return -ENOBUFS;
	}

	*buf = server->out_buf;
	server->out_buf = NULL;

	return 0;
}

/* Back to READY: the pads forget what the last negotiation or probe left on them */
static int server_reset(struct mpipe_pipeline_server *server)
{
	int ret;

	server->negotiated = false;

	ret = mpipe_element_set_state(server_pipeline_element(server), MPIPE_STATE_READY);

	/* The formats of the run that ended constrain nothing any more */
	if (server->in != NULL) {
		(void)mpipe_structure_init_any(&server->in->caps);
	}
	if (server->out != NULL) {
		(void)mpipe_structure_init_any(&server->out->caps);
	}

	return ret;
}

int mpipe_pipeline_server_init(struct mpipe_pipeline_server *server, struct mpipe *pipeline,
			       struct mpipe_app_src *in, struct mpipe_app_sink *out)
{
	const struct mpipe_buffer_pool_config pool_req = {
		.align = 1,
		.min_buffers = 1,
		.max_buffers = 1,
	};

	__ASSERT_NO_MSG(server != NULL);
	__ASSERT_NO_MSG(pipeline != NULL);
	__ASSERT_NO_MSG(in != NULL || out != NULL);

	if ((in != NULL && in->src.src_pad.peer == NULL) ||
	    (out != NULL && out->sink.sink_pad.peer == NULL)) {
		LOG_ERR("The application elements of the served pipeline are not linked");
		return -ENOTCONN;
	}

	server->pipeline = pipeline;
	server->in = in;
	server->out = out;
	server->out_buf = NULL;
	server->negotiated = false;

	/* Both pools own headers only: the memory is the client's */
	mpipe_buffer_pool_init(&server->in_pool);
	(void)mpipe_buffer_pool_set_req_config(&server->in_pool, &pool_req);

	mpipe_buffer_pool_init(&server->out_pool);
	(void)mpipe_buffer_pool_set_req_config(&server->out_pool, &pool_req);
	server->out_pool.acquire_buffer = server_out_pool_acquire;

	/*
	 * The elements may write into the client's output memory. Not with a
	 * source of their own: its buffers exist before the client asks for one.
	 */
	if (in != NULL && out != NULL) {
		return mpipe_object_set_properties((struct mpipe_object *)out,
						   MPIPE_PROP_BASE_APP_SINK_POOL, &server->out_pool,
						   MPIPE_PROP_LIST_END);
	}

	return 0;
}

int mpipe_pipeline_server_enum_caps(struct mpipe_pipeline_server *server,
				    enum mpipe_pad_direction direction, uint32_t index,
				    struct mpipe_structure *caps)
{
	struct mpipe_pad *pad;
	uint32_t found = 0;
	int ret;

	__ASSERT_NO_MSG(server != NULL);
	__ASSERT_NO_MSG(caps != NULL);

	if (direction == MPIPE_PAD_SINK && server->in != NULL) {
		pad = server_entry_pad(server);
	} else if (direction == MPIPE_PAD_SRC && server->out != NULL) {
		pad = server_exit_pad(server);
	} else {
		return -ENOTSUP;
	}

	/* The pad may skip indices; the client is given a dense numbering */
	for (uint32_t i = 0;; i++) {
		ret = mpipe_pad_enum_caps(pad, i, NULL, caps);
		if (ret == -EAGAIN) {
			continue;
		}

		if (ret != 0) {
			return ret;
		}

		if (found == index) {
			return 0;
		}

		found++;
	}
}

/*
 * Ask the pipeline what it makes of in_filter at the entry when only
 * out_filter is accepted at the exit. The answers are what the query settles
 * on at both ends.
 */
static int server_probe(struct mpipe_pipeline_server *server,
			const struct mpipe_structure *in_filter,
			const struct mpipe_structure *out_filter, struct mpipe_structure *in_caps,
			struct mpipe_structure *out_caps)
{
	struct mpipe_structure accepted = server->out->caps;
	struct mpipe_dispatch query = {
		.type = MPIPE_DISPATCH_CAPS,
		.caps = in_caps,
	};
	int ret;

	*in_caps = *in_filter;
	server->out->caps = *out_filter;

	ret = mpipe_pad_query(server_entry_pad(server), &query);

	server->out->caps = accepted;

	if (ret != 0 || mpipe_structure_is_empty(in_caps)) {
		return -ENODATA;
	}

	/* Each element left the answer of its downstream peer on its src pad */
	*out_caps = server_exit_pad(server)->caps;

	return 0;
}

int mpipe_pipeline_server_transform_caps(struct mpipe_pipeline_server *server,
					 enum mpipe_pad_direction direction, uint32_t index,
					 const struct mpipe_structure *in,
					 struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	struct mpipe_structure in_caps;
	struct mpipe_structure out_caps;
	struct mpipe_pad *pad;
	uint32_t found = 0;
	int ret;

	__ASSERT_NO_MSG(server != NULL);
	__ASSERT_NO_MSG(in != NULL);
	__ASSERT_NO_MSG(out != NULL);

	if (server->in == NULL || server->out == NULL) {
		return -ENOTSUP;
	}

	if (direction == MPIPE_PAD_SRC) {
		pad = server_exit_pad(server);
	} else if (direction == MPIPE_PAD_SINK) {
		pad = server_entry_pad(server);
	} else {
		return -EINVAL;
	}

	/* A probe writes on the pads: not under a negotiated pipeline */
	if (server_pipeline_element(server)->current_state != MPIPE_STATE_READY) {
		ret = server_reset(server);
		if (ret != 0) {
			return ret;
		}
	}

	/*
	 * The alternatives on the side asked for are what its pad enumerates;
	 * each one the pipeline can reach from the given capability is an answer.
	 */
	for (uint32_t i = 0;; i++) {
		ret = mpipe_pad_enum_caps(pad, i, NULL, &candidate);
		if (ret == -EAGAIN) {
			continue;
		}

		if (ret != 0) {
			return (ret == -ENOENT) ? -ENOENT : ret;
		}

		if (direction == MPIPE_PAD_SRC) {
			ret = server_probe(server, in, &candidate, &in_caps, &out_caps);
		} else {
			ret = server_probe(server, &candidate, in, &in_caps, &out_caps);
		}

		if (ret != 0) {
			continue;
		}

		if (found == index) {
			*out = (direction == MPIPE_PAD_SRC) ? out_caps : in_caps;
			return 0;
		}

		found++;
	}
}

int mpipe_pipeline_server_set_caps(struct mpipe_pipeline_server *server,
				   enum mpipe_pad_direction direction,
				   const struct mpipe_structure *caps)
{
	struct mpipe_element *pipeline;
	int ret;

	__ASSERT_NO_MSG(server != NULL);
	__ASSERT_NO_MSG(caps != NULL);

	if (!mpipe_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	pipeline = server_pipeline_element(server);

	/* A new format means a new negotiation, from READY */
	if (pipeline->current_state != MPIPE_STATE_READY) {
		ret = server_reset(server);
		if (ret != 0) {
			return ret;
		}
	}

	if (direction == MPIPE_PAD_SINK && server->in != NULL) {
		ret = mpipe_object_set_properties((struct mpipe_object *)server->in,
						  MPIPE_PROP_BASE_APP_SRC_CAPS, caps,
						  MPIPE_PROP_LIST_END);
		if (ret != 0 || server->out != NULL) {
			/* The exit format follows: the negotiation waits for it */
			return ret;
		}
	} else if (direction == MPIPE_PAD_SRC && server->out != NULL) {
		ret = mpipe_object_set_properties((struct mpipe_object *)server->out,
						  MPIPE_PROP_BASE_APP_SINK_CAPS, caps,
						  MPIPE_PROP_LIST_END);
		if (ret != 0) {
			return ret;
		}
	} else {
		return -ENOTSUP;
	}

	ret = mpipe_element_set_state(pipeline, MPIPE_STATE_PAUSED);
	if (ret != 0) {
		LOG_ERR("The served pipeline failed to negotiate (%d)", ret);
		(void)server_reset(server);
		return ret;
	}

	server->negotiated = true;

	return 0;
}

int mpipe_pipeline_server_get_pool_config(struct mpipe_pipeline_server *server,
					  enum mpipe_pad_direction direction,
					  struct mpipe_buffer_pool_config *cfg)
{
	ARG_UNUSED(server);
	ARG_UNUSED(direction);

	__ASSERT_NO_MSG(cfg != NULL);

	*cfg = (struct mpipe_buffer_pool_config){
		.align = (uint16_t)MAX(sys_cache_data_line_size_get(), 1U),
		.min_buffers = 1,
	};

	return 0;
}

int mpipe_pipeline_server_process(struct mpipe_pipeline_server *server, void *in, uint32_t in_size,
				  void *out, uint32_t out_capacity, uint32_t *out_size)
{
	struct net_buf *in_buf;
	struct net_buf *result = NULL;
	struct net_buf *extra;
	uint32_t used;
	int ret;

	__ASSERT_NO_MSG(server != NULL);

	/* A pipeline with a source of its own takes no input */
	if (server->in == NULL) {
		return -ENOTSUP;
	}

	if (in == NULL || in_size == 0U ||
	    (server->out != NULL && (out == NULL || out_size == NULL))) {
		return -EINVAL;
	}

	if (!server->negotiated) {
		return -ENOTCONN;
	}

	if (out_size != NULL) {
		*out_size = 0;
	}

	/* The other core wrote the input: drop what this core's cache holds of it */
	(void)sys_cache_data_invd_range(in, in_size);

	in_buf = server_wrap(&server->in_pool, in, in_size, in_size);
	if (in_buf == NULL) {
		return -ENOBUFS;
	}

	mpipe_buffer_get_meta(in_buf)->pts = mpipe_element_running_time(&server->in->src.element);

	if (server->out != NULL) {
		server->out_buf = server_wrap(&server->out_pool, out, out_capacity, 0);
		if (server->out_buf == NULL) {
			net_buf_unref(in_buf);
			return -ENOBUFS;
		}
	}

	/* This thread streams: the walk returns once every element is done with the input */
	ret = mpipe_push_buffer(&server->in->src.src_pad, in_buf);

	/* No element allocated from the proposed pool */
	if (server->out_buf != NULL) {
		net_buf_unref(server->out_buf);
		server->out_buf = NULL;
	}

	if (server->out == NULL) {
		return ret;
	}

	(void)mpipe_app_sink_pull(server->out, &result, K_NO_WAIT);

	/* One input gives one output: anything more has no place to go */
	while (mpipe_app_sink_pull(server->out, &extra, K_NO_WAIT) == 0) {
		LOG_WRN("Dropping an extra output buffer");
		net_buf_unref(extra);
	}

	if (ret != 0 || result == NULL) {
		if (result != NULL) {
			net_buf_unref(result);
		}

		return ret;
	}

	used = mpipe_buffer_get_meta(result)->bytes_used;

	/* An element that kept to its own pool: bring the result over */
	if (result->data != (uint8_t *)out) {
		if (used > out_capacity) {
			net_buf_unref(result);
			return -EMSGSIZE;
		}

		memcpy(out, result->data, used);
	}

	net_buf_unref(result);

	/* The other core reads the output next */
	(void)sys_cache_data_flush_range(out, used);

	*out_size = used;

	return 0;
}

int mpipe_pipeline_server_produce(struct mpipe_pipeline_server *server, void *out,
				  uint32_t out_capacity, uint32_t *out_size, k_timeout_t timeout)
{
	struct net_buf *result = NULL;
	uint32_t used;
	int ret;

	__ASSERT_NO_MSG(server != NULL);

	if (server->in != NULL || server->out == NULL) {
		return -ENOTSUP;
	}

	if (out == NULL || out_size == NULL) {
		return -EINVAL;
	}

	*out_size = 0;

	if (!server->negotiated) {
		return -ENOTCONN;
	}

	/* The thread of the pipeline streams: this one waits for what it delivers */
	ret = mpipe_app_sink_pull(server->out, &result, timeout);
	if (ret != 0) {
		return ret;
	}

	used = mpipe_buffer_get_meta(result)->bytes_used;
	if (used > out_capacity) {
		net_buf_unref(result);
		return -EMSGSIZE;
	}

	memcpy(out, result->data, used);
	net_buf_unref(result);

	/* The other core reads the output next */
	(void)sys_cache_data_flush_range(out, used);

	*out_size = used;

	return 0;
}

int mpipe_pipeline_server_set_state(struct mpipe_pipeline_server *server, enum mpipe_state state)
{
	__ASSERT_NO_MSG(server != NULL);

	if (state == MPIPE_STATE_READY) {
		return server_reset(server);
	}

	if (!server->negotiated) {
		return -ENOTCONN;
	}

	return mpipe_element_set_state(server_pipeline_element(server), state);
}

int mpipe_pipeline_server_eos(struct mpipe_pipeline_server *server)
{
	struct mpipe_dispatch eos_event = {.type = MPIPE_DISPATCH_EOS};
	struct net_buf *buf;
	int ret;

	__ASSERT_NO_MSG(server != NULL);

	/* The source of the pipeline, when it has one, ends the stream itself */
	if (!server->negotiated || server->in == NULL) {
		return 0;
	}

	ret = mpipe_pad_send_event(server_entry_pad(server), &eos_event);

	/* An element flushing on the event has nobody left to give its buffers to */
	while (server->out != NULL && mpipe_app_sink_pull(server->out, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}

	return ret;
}
