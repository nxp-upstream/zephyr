/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Client-side audio buffer pool for multi-core pipelines.
 * @ingroup mpipe_aud_buffer_pool_clients
 *
 * Hands out the buffers a server on another core keeps for one function
 * instance, so that the server reads and writes its own memory.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_CLIENT_H_

/**
 * @defgroup mpipe_aud_buffer_pool_clients Buffer Pool Clients
 * @ingroup mpipe_aud
 * @brief Client-side audio buffer pool helpers.
 * @{
 */

#include <stdint.h>

#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_pad.h>

/**
 * @brief One buffer of the remote core, as the client pool tracks it.
 */
struct mpipe_aud_buffer_pool_client_slot {
	/** Reserved for the k_fifo the free slots are queued on */
	void *fifo_reserved;
	/** Address of the buffer, as the remote core reported it */
	uint32_t addr;
};

/**
 * @brief RPC callback to get the address of one buffer of a remote instance.
 *
 * @param handle Function instance the buffer belongs to.
 * @param direction Pad direction the buffer belongs to.
 * @param ind Index of the buffer, below the count the remote reports.
 * @param[out] addr Address of the buffer.
 *
 * @return 0 on success or a negative errno code on failure.
 */
typedef int32_t (*mpipe_aud_buffer_pool_client_get_buf_t)(uint32_t handle,
							   enum mpipe_pad_direction direction,
							   uint8_t ind, uint32_t *addr);

/**
 * @brief Client-side audio buffer pool structure.
 *
 * The buffers live in memory the remote core owns and keeps for one function
 * instance. The pool keeps only their addresses, on the client core, and
 * wraps one in a net_buf on each acquire. Starting the pool asks the remote
 * for the address of each buffer of the instance.
 *
 * The pool always hands out every buffer the remote has, whatever
 * min_buffers was negotiated: the count is the remote's to choose, and an
 * acquire waits for a release when all of them are in use. A negotiated
 * config asking for more buffers than the remote has, or for an alignment
 * that does not divide the remote one, is refused.
 */
struct mpipe_aud_buffer_pool_client {
	/** Base buffer pool structure */
	struct mpipe_buffer_pool pool;
	/** Pad direction of the remote buffers, see @ref mpipe_pad_direction */
	enum mpipe_pad_direction direction;
	/** Handle of the remote function instance the buffers belong to */
	uint32_t handle;
	/**
	 * Number of buffers the remote provides for @ref direction, at most
	 * @kconfig{CONFIG_MPIPE_AUD_BUFFER_POOL_CLIENT_NUM_MAX}
	 */
	uint8_t num_bufs;
	/** RPC callback returning the address of one remote buffer */
	mpipe_aud_buffer_pool_client_get_buf_t get_buf_rpc;
	/** Slots not wrapped in a net_buf at the moment */
	struct k_fifo free_fifo;
	/** One slot per remote buffer */
	struct mpipe_aud_buffer_pool_client_slot slots[CONFIG_MPIPE_AUD_BUFFER_POOL_CLIENT_NUM_MAX];
};

/**
 * @brief Initialize a client-side audio buffer pool.
 *
 * mpipe_aud_buffer_pool_client_setup() must be called before the pool is
 * started.
 *
 * @param pool Pointer to the @ref mpipe_buffer_pool to initialize.
 * @param direction Pad direction of the remote buffers.
 */
void mpipe_aud_buffer_pool_client_init(struct mpipe_buffer_pool *pool,
				       enum mpipe_pad_direction direction);

/**
 * @brief Describe the remote buffers a client-side audio buffer pool hands out.
 *
 * Sets the instance, the buffer count and the size, alignment and count
 * limits the pool requires, as the remote reported them. Call it once, before
 * the pool is started.
 *
 * @param pool Pointer to the @ref mpipe_buffer_pool, initialized with
 *             mpipe_aud_buffer_pool_client_init().
 * @param handle Remote function instance the buffers belong to.
 * @param num_bufs Number of buffers the remote provides.
 * @param size Size of each remote buffer, in bytes.
 * @param align Alignment of the remote buffers, in bytes.
 * @param get_buf_rpc RPC callback returning the address of one remote buffer.
 *
 * @retval 0 Success.
 * @retval -ENODEV The remote has no buffer, or no callback was given.
 * @retval -ENOSPC The remote has more buffers than
 *         @kconfig{CONFIG_MPIPE_AUD_BUFFER_POOL_CLIENT_NUM_MAX}.
 * @retval -EBUSY The pool is started.
 */
int mpipe_aud_buffer_pool_client_setup(struct mpipe_buffer_pool *pool, uint32_t handle,
				       uint8_t num_bufs, uint32_t size, uint16_t align,
				       mpipe_aud_buffer_pool_client_get_buf_t get_buf_rpc);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_CLIENT_H_ */
