/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/mpipe/mpipe_buffer.h>

NET_BUF_POOL_FIXED_DEFINE(mpipe_buf_pool, CONFIG_MPIPE_NET_BUF_POOL_COUNT, 1,
			  sizeof(struct mpipe_buffer_meta), mpipe_buffer_destroy);

void mpipe_buffer_destroy(struct net_buf *buf)
{
	struct mpipe_buffer_meta *bm;
	struct net_buf *parent;

	__ASSERT_NO_MSG(buf != NULL);

	bm = mpipe_buffer_get_meta(buf);
	if (bm->pool != NULL && bm->pool->release_buffer != NULL) {
		bm->pool->release_buffer(bm->pool, buf);
	}

	/* The header goes back first: the parent may be the last thing holding it */
	parent = bm->parent;
	bm->parent = NULL;

	net_buf_destroy(buf);

	if (parent != NULL) {
		net_buf_unref(parent);
	}
}

struct net_buf *mpipe_buffer_view(struct net_buf *parent, uint32_t offset, uint32_t len,
				  k_timeout_t timeout)
{
	const struct mpipe_buffer_meta *pm;
	struct mpipe_buffer_meta *vm;
	struct net_buf *view;

	__ASSERT_NO_MSG(parent != NULL);

	pm = mpipe_buffer_get_meta(parent);
	if (offset > pm->bytes_used || len > pm->bytes_used - offset) {
		return NULL;
	}

	view = net_buf_alloc_with_data(&mpipe_buf_pool, parent->data + offset, len, timeout);
	if (view == NULL) {
		return NULL;
	}

	vm = mpipe_buffer_get_meta(view);
	vm->pool = NULL;
	vm->bytes_used = len;
	vm->timestamp = pm->timestamp;
	vm->driver_buf = NULL;
	vm->priv = NULL;
	vm->parent = net_buf_ref(parent);

	return view;
}

int mpipe_buffer_pool_configure(struct mpipe_buffer_pool *pool, const struct mpipe_structure *caps)
{
	__ASSERT_NO_MSG(pool != NULL);

	if (pool->configure == NULL) {
		return -ENOSYS;
	}

	return pool->configure(pool, caps);
}

int mpipe_buffer_pool_set_req_config(struct mpipe_buffer_pool *pool,
				     const struct mpipe_buffer_pool_config *cfg)
{
	__ASSERT_NO_MSG(pool != NULL);
	__ASSERT_NO_MSG(cfg != NULL);

	if (pool->started) {
		return -EBUSY;
	}

	pool->req_config = *cfg;
	pool->config = *cfg;

	return 0;
}

int mpipe_buffer_pool_set_config(struct mpipe_buffer_pool *pool,
				 const struct mpipe_buffer_pool_config *cfg)
{
	__ASSERT_NO_MSG(pool != NULL);
	__ASSERT_NO_MSG(cfg != NULL);

	if (pool->started) {
		return -EBUSY;
	}

	if (pool->set_config != NULL) {
		return pool->set_config(pool, cfg);
	}

	pool->config = *cfg;

	return 0;
}

int mpipe_buffer_pool_start(struct mpipe_buffer_pool *pool)
{
	int ret;

	__ASSERT_NO_MSG(pool != NULL);

	if (pool->started) {
		return 0;
	}

	if (pool->start == NULL) {
		pool->started = true;
		return -ENOSYS;
	}

	ret = pool->start(pool);
	if (ret == 0) {
		pool->started = true;
	}

	return ret;
}

int mpipe_buffer_pool_stop(struct mpipe_buffer_pool *pool)
{
	int ret;
	bool was_started;

	__ASSERT_NO_MSG(pool != NULL);

	was_started = pool->started;
	pool->started = false;
	pool->config = pool->req_config;

	if (!was_started) {
		return 0;
	}

	if (pool->stop == NULL) {
		return -ENOSYS;
	}

	ret = pool->stop(pool);
	if (ret != 0) {
		pool->started = true;
	}

	return ret;
}

void mpipe_buffer_pool_init(struct mpipe_buffer_pool *pool)
{
	__ASSERT_NO_MSG(pool != NULL);

	pool->nb_pool = &mpipe_buf_pool;
	pool->started = false;
}
