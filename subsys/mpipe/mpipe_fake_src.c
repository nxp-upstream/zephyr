/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_fake_src.h>

LOG_MODULE_REGISTER(mpipe_fake_src, CONFIG_MPIPE_LOG_LEVEL);

NET_BUF_POOL_FIXED_DEFINE(mpipe_fake_src_pool, 1, CONFIG_MPIPE_FAKE_SRC_BUF_SZ,
			  sizeof(struct mpipe_buffer_meta), mpipe_buffer_destroy);

static int mpipe_fake_src_pool_acquire(struct mpipe_buffer_pool *pool, struct net_buf **buf)
{
	struct net_buf *nb;
	struct mpipe_buffer_meta *meta;

	if (pool == NULL || buf == NULL || pool->nb_pool == NULL) {
		return -EINVAL;
	}

	nb = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_NO_WAIT);
	if (nb == NULL) {
		LOG_ERR("Failed to acquire buffer from the pool");
		return -ENOBUFS;
	}

	nb->len = pool->config.size;

	meta = mpipe_buffer_get_meta(nb);
	meta->pool = pool;
	meta->bytes_used = pool->config.size;
	meta->timestamp = k_uptime_get_32();

	*buf = nb;

	return 0;
}

static int mpipe_fake_src_pool_release(struct mpipe_buffer_pool *pool, struct net_buf *buf)
{
	ARG_UNUSED(pool);

	if (buf == NULL) {
		return 0;
	}

	struct mpipe_buffer_meta *meta = mpipe_buffer_get_meta(buf);

	if (meta != NULL) {
		meta->bytes_used = 0;
		meta->timestamp = 0;
		meta->driver_buf = NULL;
		meta->priv = NULL;
	}

	buf->len = 0;

	return 0;
}

void mpipe_fake_src_init(struct mpipe_element *self)
{
	struct mpipe_fake_src *fsrc = (struct mpipe_fake_src *)self;

	mpipe_src_init(self);

	mpipe_buffer_pool_init(&fsrc->pool);
	fsrc->pool.nb_pool = &mpipe_fake_src_pool;
	fsrc->pool.config.min_buffers = 1;
	fsrc->pool.config.size = CONFIG_MPIPE_FAKE_SRC_BUF_SZ;
	fsrc->pool.acquire_buffer = mpipe_fake_src_pool_acquire;
	fsrc->pool.release_buffer = mpipe_fake_src_pool_release;

	fsrc->src.pool = &fsrc->pool;
}
