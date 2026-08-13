/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Buffer and buffer pool APIs.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_BUFFER_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_BUFFER_H_

/**
 * @defgroup mpipe_buffer Buffers
 * @ingroup mpipe_framework
 * @brief Buffer metadata and buffer-pool APIs.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/net_buf.h>

/**
 * @brief Buffer pool config structure
 */
struct mpipe_buffer_pool_config {
	/** Size of each buffer in bytes */
	uint32_t size;
	/** Alignment of each buffer in bytes */
	uint16_t align;
	/** Minimum number of buffers for the pool to work */
	uint8_t min_buffers;
	/** Maximum number of buffers in the pool */
	uint8_t max_buffers;
};

struct mpipe_structure;

/**
 * @brief Buffer pool structure
 */
struct mpipe_buffer_pool {
	/** Configuration parameters of the buffer pool */
	struct mpipe_buffer_pool_config config;
	/** net_buf pool associated with the buffer pool */
	struct net_buf_pool *nb_pool;

	/** Configure the pool with the given caps structure */
	int (*configure)(struct mpipe_buffer_pool *pool, struct mpipe_structure *config);
	/**
	 * Apply a negotiated pool config. The implementation validates @p cfg
	 * against what the pool can provide, writes the accepted values into
	 * @ref mpipe_buffer_pool::config itself, and returns a negative errno
	 * to refuse (e.g. -ENOTSUP for an alignment the backing allocator
	 * cannot honor, -ENOSPC for a buffer count beyond the pool capacity).
	 */
	int (*set_config)(struct mpipe_buffer_pool *pool,
			  const struct mpipe_buffer_pool_config *cfg);
	/** Start the pool and allocate resources */
	int (*start)(struct mpipe_buffer_pool *pool);
	/** Stop the pool and free resources */
	int (*stop)(struct mpipe_buffer_pool *pool);
	/** Acquire a buffer from the pool */
	int (*acquire_buffer)(struct mpipe_buffer_pool *pool, struct net_buf **buf);
	/**
	 * Release a buffer back to the pool, automatically called when
	 * buffer refcount reaches 0.
	 */
	int (*release_buffer)(struct mpipe_buffer_pool *pool, struct net_buf *buf);

	/** Flag indicating if the pool has been started */
	bool started;
};

/**
 * @brief Common buffer metadata stored in net_buf user_data.
 */
struct mpipe_buffer_meta {
	/** Buffer pool this buffer belongs to. */
	struct mpipe_buffer_pool *pool;
	/** Valid payload in bytes. */
	uint32_t bytes_used;
	/** Timestamp in milliseconds. */
	uint32_t timestamp;
	/** Pointer to the real driver-own buffer. */
	void *driver_buf;
	/** Opaque pointer for plugin-specific usage. */
	void *priv;
};

/** Get buffer metadata */
static inline struct mpipe_buffer_meta *mpipe_buffer_get_meta(struct net_buf *buf)
{
	return (struct mpipe_buffer_meta *)net_buf_user_data(buf);
}

/**
 * @brief Destroy callback for net_buf.
 *
 * Automatically called when buffer reference count reaches zero.
 *
 * @param buf Pointer to the net_buf to destroy.
 */
void mpipe_buffer_destroy(struct net_buf *buf);

/**
 * @brief Helper to configure a buffer pool
 *
 * @param pool Pointer to the buffer pool to configure
 * @param config Caps structure to configure the buffer pool
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_buffer_pool_configure(struct mpipe_buffer_pool *pool, struct mpipe_structure *config);

/**
 * @brief Apply a negotiated config to a buffer pool
 *
 * This is the only way a config negotiated through an allocation query may
 * reach a pool: the caller hands the value over and the pool's owner
 * validates it. An element must never write @ref mpipe_buffer_pool::config
 * fields of a pool it does not own.
 *
 * With a @ref mpipe_buffer_pool::set_config implementation, the pool
 * decides: it may accept, clamp, or refuse the config. Without one, the
 * config is copied in as given.
 *
 * @param pool Pointer to the buffer pool
 * @param cfg  Negotiated config to apply
 *
 * @retval 0       Success, the pool's config reflects the accepted values.
 * @retval -EINVAL @p pool or @p cfg is NULL.
 * @retval -EBUSY  The pool is started; stop it before reconfiguring.
 * @return Any negative errno the pool's set_config returns to refuse.
 */
int mpipe_buffer_pool_set_config(struct mpipe_buffer_pool *pool,
				 const struct mpipe_buffer_pool_config *cfg);

/**
 * @brief Helper to start a buffer pool
 *
 * @param pool Pointer to the buffer pool to start
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_buffer_pool_start(struct mpipe_buffer_pool *pool);

/**
 * @brief Helper to stop a buffer pool
 *
 * @param pool Pointer to the buffer pool to stop
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_buffer_pool_stop(struct mpipe_buffer_pool *pool);

/**
 * @brief Initialize a buffer pool
 *
 * @param pool Pointer to the buffer pool to initialize
 */
void mpipe_buffer_pool_init(struct mpipe_buffer_pool *pool);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_BUFFER_H_ */
