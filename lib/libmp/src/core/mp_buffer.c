/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mp_buffer.h"

static void mp_buffer_destroy(struct net_buf *buf)
{
	struct mp_buffer_meta *bm;

	if (buf == NULL) {
		return;
	}

	bm = mp_buffer_get_meta(buf);
	if (bm != NULL && bm->pool != NULL && bm->pool->release_buffer != NULL) {
		bm->pool->release_buffer(bm->pool, buf);
	}

	net_buf_destroy(buf);
}

NET_BUF_POOL_FIXED_DEFINE(mp_buf_pool, CONFIG_MP_BUF_POOL_NUM_BUFS, 1,
			  sizeof(struct mp_buffer_meta), mp_buffer_destroy);

void mp_buffer_pool_init(struct mp_buffer_pool *pool)
{
	if (pool == NULL) {
		return;
	}

	pool->nb_pool = &mp_buf_pool;
}
