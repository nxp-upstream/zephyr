/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Buffer and buffer pool APIs.
 */

#ifndef __MP_BUFFER_H__
#define __MP_BUFFER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/net_buf.h>

#include "mp_structure.h"

#define MP_BUFFER(buf)       ((struct net_buf *)(buf))
#define MP_BUFFER_POOL(pool) ((struct mp_buffer_pool *)(pool))

/**
 * @brief Buffer pool config structure
 */
struct mp_buffer_pool_config {
	/** Size of each buffer in bytes */
	uint32_t size;
	/** Alignment of each buffer in bytes */
	uint16_t align;
	/** Minimum number of buffers for the pool to work */
	uint8_t min_buffers;
	/** Maximum number of buffers in the pool */
	uint8_t max_buffers;
};

/**
 * @brief Buffer pool structure
 */
struct mp_buffer_pool {
	struct mp_buffer_pool_config config;

	/** net_buf pool used to allocate buffer wrappers (net_buf objects). */
	struct net_buf_pool *nb_pool;

	/* Pool operations */
	/** Configure the pool with given parameters */
	int (*configure)(struct mp_buffer_pool *pool, struct mp_structure *config);
	/** Start the pool and allocate resources */
	int (*start)(struct mp_buffer_pool *pool);
	/** Stop the pool and free resources */
	int (*stop)(struct mp_buffer_pool *pool);
	/** Acquire a buffer from the pool */
	int (*acquire_buffer)(struct mp_buffer_pool *pool, struct net_buf **buf);
	/** Release a buffer back to the pool, automatically called when refcount reaches 0 */
	int (*release_buffer)(struct mp_buffer_pool *pool, struct net_buf *buf);
};

/**
 * @brief Common buffer metadata stored in net_buf user_data.
 */
struct mp_buffer_meta {
	/** Buffer pool this buffer belongs to. */
	struct mp_buffer_pool *pool;
	/** Valid payload in bytes. */
	uint32_t bytes_used;
	/** Timestamp in milliseconds. */
	uint32_t timestamp;
	/** Opaque pointer for plugin-specific usage. */
	void *priv;
};

/** Get buffer metadata */
static inline struct mp_buffer_meta *mp_buffer_get_meta(struct net_buf *buf)
{
	return (struct mp_buffer_meta *)net_buf_user_data(buf);
}

/**
 * @brief Initialize a buffer pool
 *
 * Sets up the buffer pool with default function pointers for pool operations.
 *
 * @param pool Pointer to the buffer pool to initialize
 */
void mp_buffer_pool_init(struct mp_buffer_pool *pool);

#endif /* __MP_BUFFER_H__ */
