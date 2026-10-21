/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>

#include <zephyr/mpipe/net/mpipe_socket_src.h>

/* Investigation aid, see mpipe_stage_stats.h */
#define MPIPE_STAGE_STATS 1
#if MPIPE_STAGE_STATS
#include "../mpipe_stage_stats.h"
static struct mpipe_stage_stats rx_stats;
#endif

LOG_MODULE_REGISTER(mpipe_socket_src, CONFIG_MPIPE_LOG_LEVEL);

NET_BUF_POOL_FIXED_DEFINE(mpipe_socket_src_nb_pool, CONFIG_MPIPE_NET_SRC_NUM_BUFS,
			  CONFIG_MPIPE_NET_SRC_BUF_SIZE, sizeof(struct mpipe_buffer_meta),
			  mpipe_buffer_destroy);

static void mpipe_socket_src_close(struct mpipe_socket_src *ssrc)
{
	if (ssrc->fd >= 0) {
		(void)zsock_close(ssrc->fd);
		ssrc->fd = -1;
	}
}

int mpipe_socket_src_set_property(struct mpipe_object *obj, uint32_t key, const void *val)
{
	struct mpipe_socket_src *ssrc = (struct mpipe_socket_src *)obj;

	switch (key) {
	case MPIPE_PROP_SOCKET_SRC_FD:
		if (val == NULL) {
			return -EINVAL;
		}

		if (ssrc->src.element.current_state == MPIPE_STATE_PLAYING) {
			LOG_ERR("The socket cannot change while playing");
			return -EBUSY;
		}

		mpipe_socket_src_close(ssrc);
		ssrc->fd = *(const int *)val;
		return 0;
	default:
		return mpipe_src_set_property(obj, key, val);
	}
}

int mpipe_socket_src_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_socket_src *ssrc = (struct mpipe_socket_src *)obj;

	switch (key) {
	case MPIPE_PROP_SOCKET_SRC_FD:
		*(int *)val = ssrc->fd;
		return 0;
	default:
		return mpipe_src_get_property(obj, key, val);
	}
}

/*
 * Receive into the current chunk and push the new bytes as a view of it. The
 * chunk is left once the room in it is too small for a useful read; it
 * returns to the pool when the last view of it, typically the last frame cut
 * out of it downstream, is released.
 */
static int mpipe_socket_src_pool_acquire(struct mpipe_buffer_pool *pool, struct net_buf **buf)
{
	struct mpipe_socket_src *ssrc = CONTAINER_OF(pool, struct mpipe_socket_src, pool);
	struct mpipe_buffer_meta *meta;
	struct net_buf *chunk;
	struct net_buf *view;
	uint32_t fill;
	ssize_t rd;

	if (ssrc->fd < 0) {
		return -ENOTCONN;
	}

	chunk = ssrc->chunk;
	if (chunk != NULL && chunk->size - mpipe_buffer_get_meta(chunk)->bytes_used <
				     CONFIG_MPIPE_NET_SRC_MIN_READ) {
		net_buf_unref(chunk);
		chunk = NULL;
		ssrc->chunk = NULL;
	}

	if (chunk == NULL) {
		/* Blocks while every chunk is pinned by frames not consumed yet */
		chunk = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_FOREVER);
		if (chunk == NULL) {
			return -ENOBUFS;
		}

		meta = mpipe_buffer_get_meta(chunk);
		meta->pool = pool;
		meta->bytes_used = 0;
		meta->timestamp = 0;
		meta->driver_buf = NULL;
		meta->priv = NULL;
		meta->parent = NULL;
		chunk->len = 0;
		ssrc->chunk = chunk;
	}

	meta = mpipe_buffer_get_meta(chunk);
	fill = meta->bytes_used;

#if MPIPE_STAGE_STATS
	uint32_t t0 = k_cycle_get_32();
#endif

	rd = zsock_recv(ssrc->fd, chunk->data + fill, chunk->size - fill, 0);

#if MPIPE_STAGE_STATS
	if (rd > 0 && mpipe_stage_stats_add(&rx_stats, 1, (uint32_t)rd, (uint32_t)rd,
					    mpipe_stage_stats_elapsed_us(t0))) {
		LOG_INF("stats socket_src: %u KB/s, %u recv/s, %u..%u B, %u ms in recv (%u ms)",
			mpipe_stage_stats_per_s(&rx_stats, rx_stats.bytes) / 1024U,
			mpipe_stage_stats_per_s(&rx_stats, rx_stats.count),
			rx_stats.bytes / rx_stats.count, rx_stats.bytes_max,
			rx_stats.us_sum / 1000U, rx_stats.elapsed_ms);
		mpipe_stage_stats_reset(&rx_stats);
	}
#endif

	if (rd <= 0) {
		/* A closed connection is the end of the stream */
		if (rd < 0) {
			/* The socket was closed under the read: a teardown, not a failure */
			if (errno == EINTR || errno == EBADF) {
				return -EPIPE;
			}

			LOG_ERR("recv() failed (%d)", errno);
			return -errno;
		}

		return -ENODATA;
	}

	meta->bytes_used = fill + (uint32_t)rd;
	meta->timestamp = k_uptime_get_32();
	chunk->len = meta->bytes_used;

	view = mpipe_buffer_view(chunk, fill, (uint32_t)rd, K_FOREVER);
	if (view == NULL) {
		return -ENOBUFS;
	}

	*buf = view;

	return 0;
}

int mpipe_socket_src_change_state(struct mpipe_element *self, enum mpipe_state_change transition)
{
	struct mpipe_socket_src *ssrc = (struct mpipe_socket_src *)self;
	int ret = mpipe_src_change_state(self, transition);

	if (ret != 0) {
		return ret;
	}

	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		mpipe_socket_src_close(ssrc);
		if (ssrc->chunk != NULL) {
			net_buf_unref(ssrc->chunk);
			ssrc->chunk = NULL;
		}
	}

	return 0;
}

int mpipe_socket_src_init(struct mpipe_socket_src *ssrc, uint8_t id)
{
	const struct mpipe_buffer_pool_config pool_req = {
		.size = CONFIG_MPIPE_NET_SRC_BUF_SIZE,
		.min_buffers = CONFIG_MPIPE_NET_SRC_NUM_BUFS,
	};
	struct mpipe_element *self = &ssrc->src.element;
	int ret = mpipe_src_init(&ssrc->src, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "socket_src");

	self->object.set_property = mpipe_socket_src_set_property;
	self->object.get_property = mpipe_socket_src_get_property;
	self->change_state = mpipe_socket_src_change_state;

	mpipe_buffer_pool_init(&ssrc->pool);
	ssrc->pool.nb_pool = &mpipe_socket_src_nb_pool;
	(void)mpipe_buffer_pool_set_req_config(&ssrc->pool, &pool_req);
	ssrc->pool.acquire_buffer = mpipe_socket_src_pool_acquire;
	ssrc->src.pool = &ssrc->pool;

	ssrc->chunk = NULL;
	ssrc->fd = -1;

	return 0;
}
