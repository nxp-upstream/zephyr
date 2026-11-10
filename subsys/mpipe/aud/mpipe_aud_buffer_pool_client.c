/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool_client.h>

LOG_MODULE_REGISTER(mpipe_aud_buffer_pool_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_aud_buffer_pool_client_start(struct mpipe_buffer_pool *pool)
{
	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;

	if (abpc->get_buf_rpc == NULL || abpc->num_bufs == 0U) {
		LOG_ERR("No remote buffers to hand out");
		return -ENODEV;
	}

	/* The owner checks the count against the slots when it sets num_bufs */
	__ASSERT_NO_MSG(abpc->num_bufs <= ARRAY_SIZE(abpc->slots));

	/* Drain anything left from a previous run before refilling */
	while (k_fifo_get(&abpc->free_fifo, K_NO_WAIT) != NULL) {
	}

	for (uint8_t i = 0; i < abpc->num_bufs; i++) {
		struct mpipe_aud_buffer_pool_client_slot *slot = &abpc->slots[i];

		if (abpc->get_buf_rpc(abpc->handle, abpc->direction, i, &slot->addr) != 0 ||
		    slot->addr == 0U) {
			LOG_ERR("Unable to get remote buffer %u", i);
			return -EIO;
		}

		k_fifo_put(&abpc->free_fifo, slot);
	}

	return 0;
}

static int mpipe_aud_buffer_pool_client_set_config(struct mpipe_buffer_pool *pool,
						   const struct mpipe_buffer_pool_config *cfg)
{
	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;

	/*
	 * The size follows the format and the remote fixes the alignment: keep
	 * both. A demand is met only when it divides the remote alignment.
	 */
	if (cfg->align != 0U &&
	    (pool->config.align == 0U || (pool->config.align % cfg->align) != 0U)) {
		return -ENOTSUP;
	}

	/*
	 * An acquire waits for a release when every remote buffer is out, so a
	 * peer that holds more than the remote has would wait forever: refuse.
	 */
	if (cfg->min_buffers > abpc->num_bufs) {
		return -ENOSPC;
	}

	pool->config.min_buffers = cfg->min_buffers;

	return 0;
}

static int mpipe_aud_buffer_pool_client_stop(struct mpipe_buffer_pool *pool)
{
	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;

	while (k_fifo_get(&abpc->free_fifo, K_NO_WAIT) != NULL) {
	}

	return 0;
}

static int mpipe_aud_buffer_pool_client_acquire_buffer(struct mpipe_buffer_pool *pool,
						       struct net_buf **buf)
{
	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;
	struct mpipe_aud_buffer_pool_client_slot *slot;
	struct mpipe_buffer_meta *m;
	struct net_buf *nb;

	__ASSERT_NO_MSG(buf != NULL);

	slot = k_fifo_get(&abpc->free_fifo, K_FOREVER);
	if (slot == NULL) {
		return -ENOBUFS;
	}

	/* The payload stays in the remote's memory: only its address is wrapped */
	nb = net_buf_alloc_with_data(pool->nb_pool, (void *)(uintptr_t)slot->addr,
				     pool->config.size, K_NO_WAIT);
	if (nb == NULL) {
		k_fifo_put(&abpc->free_fifo, slot);
		return -ENOBUFS;
	}

	m = mpipe_buffer_get_meta(nb);
	m->pool = pool;
	m->driver_buf = NULL;
	m->priv = slot;
	m->bytes_used = 0U;
	m->timestamp = 0U;
	nb->len = 0U;

	*buf = nb;

	return 0;
}

static int mpipe_aud_buffer_pool_client_release_buffer(struct mpipe_buffer_pool *pool,
						       struct net_buf *buf)
{
	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;
	struct mpipe_aud_buffer_pool_client_slot *slot = mpipe_buffer_get_meta(buf)->priv;

	if (slot == NULL) {
		return -EINVAL;
	}

	k_fifo_put(&abpc->free_fifo, slot);

	return 0;
}

int mpipe_aud_buffer_pool_client_setup(struct mpipe_buffer_pool *pool, uint32_t handle,
				       uint8_t num_bufs, uint32_t size, uint16_t align,
				       mpipe_aud_buffer_pool_client_get_buf_t get_buf_rpc)
{
	__ASSERT_NO_MSG(pool != NULL);

	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;
	struct mpipe_buffer_pool_config cfg = {0};

	if (get_buf_rpc == NULL || num_bufs == 0U || size == 0U) {
		LOG_ERR("Remote reported no buffer");
		return -ENODEV;
	}

	if (num_bufs > ARRAY_SIZE(abpc->slots)) {
		LOG_ERR("Remote has %u buffers, MPIPE_AUD_BUFFER_POOL_CLIENT_NUM_MAX allows %u",
			num_bufs, (unsigned int)ARRAY_SIZE(abpc->slots));
		return -ENOSPC;
	}

	abpc->handle = handle;
	abpc->num_bufs = num_bufs;
	abpc->get_buf_rpc = get_buf_rpc;

	/*
	 * Processing is synchronous, so the element holds one buffer of the
	 * pool at a time: that is the demand. What the remote has is the most
	 * the pool can provide, and its size the most a format can use.
	 */
	cfg.size = size;
	cfg.align = align;
	cfg.min_buffers = 1U;
	cfg.max_buffers = num_bufs;

	return mpipe_buffer_pool_set_req_config(pool, &cfg);
}

void mpipe_aud_buffer_pool_client_init(struct mpipe_buffer_pool *pool,
				       enum mpipe_pad_direction direction)
{
	__ASSERT_NO_MSG(pool != NULL);

	struct mpipe_aud_buffer_pool_client *abpc = (struct mpipe_aud_buffer_pool_client *)pool;

	k_fifo_init(&abpc->free_fifo);
	abpc->direction = direction;
	abpc->handle = 0U;
	abpc->num_bufs = 0U;
	abpc->get_buf_rpc = NULL;

	mpipe_buffer_pool_init(pool);

	pool->set_config = mpipe_aud_buffer_pool_client_set_config;
	pool->start = mpipe_aud_buffer_pool_client_start;
	pool->stop = mpipe_aud_buffer_pool_client_stop;
	pool->acquire_buffer = mpipe_aud_buffer_pool_client_acquire_buffer;
	pool->release_buffer = mpipe_aud_buffer_pool_client_release_buffer;
}
