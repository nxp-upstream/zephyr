/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio buffer pool header file.
 * @ingroup mpipe_aud_buffer_pools
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_H_

/**
 * @defgroup mpipe_aud_buffer_pools Buffer Pools
 * @ingroup mpipe_aud
 * @brief Audio buffer pool helpers.
 * @{
 */

#include <zephyr/device.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/aud/mpipe_aud.h>

/**
 * @struct mpipe_aud_buffer_pool
 * @brief Audio buffer pool structure
 *
 * This structure manages memory allocation and buffer handling for audio
 * data processing within the plugin. The blocks come from one static store,
 * cut into @kconfig{CONFIG_MPIPE_AUD_BUFFER_POOL_COUNT} slices of
 * @kconfig{CONFIG_MPIPE_AUD_BUFFER_POOL_SZ_MAX} times
 * @kconfig{CONFIG_MPIPE_AUD_BUFFER_POOL_NUM_MAX} bytes. A slice belongs to
 * the memory slab the application provides, for as long as the program runs,
 * so the pools that share a slab share a slice, and a pool initialized again
 * over the same slab finds its slice.
 */
struct mpipe_aud_buffer_pool {
	/** Base buffer pool structure */
	struct mpipe_buffer_pool pool;
	/** Pointer to the associated audio device */
	const struct device *aud_dev;
	/** Memory slab for efficient buffer allocation */
	struct k_mem_slab *mem_slab;
	/**
	 * Per-chunk pointers into the mem_slab backing buffer. Bounded by the
	 * same Kconfig that sizes the pool, so the table needs no allocation.
	 */
	void *blocks[CONFIG_MPIPE_AUD_BUFFER_POOL_NUM_MAX];
};

/**
 * @brief Initialize an audio buffer pool
 *
 * This function initializes the audio buffer pool with default
 * values and sets up the function pointers.
 *
 * @param pool Pointer to the buffer pool structure to initialize.
 */
void mpipe_aud_buffer_pool_init(struct mpipe_buffer_pool *pool);

/**
 * @brief Mark a block as handed to the audio driver.
 *
 * An audio driver that is given a block, as @c i2s_write() is, frees it to
 * the slab itself once it is done with it. A sink calls this before releasing
 * the buffer that wraps such a block, so that the pool does not free the
 * block a second time. A block never handed over, dropped by a queue for
 * instance, is freed by the pool when its buffer is released.
 *
 * @param buf Buffer wrapping the block, from an audio buffer pool.
 */
void mpipe_aud_buffer_pool_hand_over(struct net_buf *buf);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_BUFFER_POOL_H_ */
