/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for zvid buffer pool.
 */

#ifndef __MP_ZVID_BUFFER_POOL_H__
#define __MP_ZVID_BUFFER_POOL_H__

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>

#include <src/core/mp_buffer.h>

#define MP_ZVID_BUFFERPOOL(self) ((struct mp_zvid_buffer_pool *)self)

struct mp_zvid_object;

/**
 * @brief Video Buffer Pool structure
 *
 * This structure represents a specialized buffer pool for video operations.
 * It extends the generic @ref struct mp_buffer_pool with video-specific functionality.
 *
 * The video buffer pool manages video buffers handling buffer allocation,
 * queuing, and dequeuing buffers through the Zephyr video subsystem.
 */
struct mp_zvid_buffer_pool {
	/** Base buffer pool structure */
	struct mp_buffer_pool pool;
	/** Associated video object */
	struct mp_zvid_object *zvid_obj;
	/** Array of maximum video buffer pointers managed by the pool */
	struct video_buffer *vbufs[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX];
	/** Number of actual video buffers in this pool */
	uint8_t vbuf_count;

	/** FIFO queue containing available buffers can be acquired by the upstream element.
	 * This is only used only by the input pool (VIDEO_BUF_TYPE_INPUT).
	 */
	struct k_fifo free_fifo;
};

/**
 * @brief Initialize a Zephyr video buffer pool
 *
 * @param pool Pointer to the @ref struct mp_buffer_pool structure to initialize
 * @param obj Pointer to the @ref struct mp_zvid_object to associate with this pool
 *
 */
void mp_zvid_buffer_pool_init(struct mp_buffer_pool *pool, struct mp_zvid_object *obj);

#endif /* __MP_ZVID_BUFFER_POOL_H__ */
