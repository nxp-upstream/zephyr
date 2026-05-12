/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Queue element for pipeline-level threading.
 *
 * The queue element decouples a pipeline into two segments running on
 * separate threads. Upstream deposits buffers into the queue's FIFO;
 * a dedicated downstream thread pulls buffers and drives the rest of
 * the pipeline.
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_QUEUE_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_QUEUE_H_

#include <zephyr/kernel.h>

#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_property.h>
#include <zephyr/mp/core/mp_thread.h>
#include <zephyr/mp/core/mp_transform.h>

/**
 * @defgroup mp_queue Queue
 * @brief Pipeline-level threading element
 * @{
 */

/** @brief Cast a pointer to a @ref mp_queue pointer. */
#define MP_QUEUE(self) ((struct mp_queue *)(self))

/**
 * @brief Queue Property Identifiers
 */
enum {
	/** Maximum number of buffers the queue can hold (0 = unlimited) */
	PROP_QUEUE_MAX_SIZE = PROP_TRANSFORM_LAST + 1,
};

/**
 * @brief Queue Element Structure
 *
 * The queue element acts as a thread boundary in a pipeline. Its chainfn
 * deposits incoming buffers into a k_fifo and returns NULL to break the
 * upstream pipeline loop. A dedicated thread pulls buffers from the FIFO
 * and drives downstream elements via pad linkage.
 */
struct mp_queue {
	/** Base transform element */
	struct mp_transform transform;
	/** FIFO for buffering between upstream and downstream threads */
	struct k_fifo fifo;
	/** Dedicated thread for downstream processing */
	struct mp_thread thread;
	/** Counting semaphore for backpressure (only used when max_size > 0) */
	struct k_sem sem;
	/** Maximum number of buffers the queue can hold (0 = unlimited / no backpressure) */
	uint16_t max_size;
};

/**
 * @brief Initialize a queue element
 *
 * @param self Pointer to the @ref mp_element to initialize as a queue
 */
void mp_queue_init(struct mp_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_QUEUE_H_ */
