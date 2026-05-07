/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Main header for mp_pipeline.
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_PIPELINE_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_PIPELINE_H_

#include <stdint.h>

#include <zephyr/mp/core/mp_bin.h>
#include <zephyr/mp/core/mp_task.h>

/**
 * @{
 */

#define MP_PIPELINE(self) ((struct mp_pipeline *)self)

/**
 * @brief struct mp_pipeline structure
 *
 * Contains all the state and timing information needed to manage
 * a complete media processing pipeline.
 */
struct mp_pipeline {
	/** Base bin container */
	struct mp_bin bin;
	/** Task associated with the pipeline */
	struct mp_task task;
	/** The running time - total time spent in PLAYING state without being flushed */
	uint64_t stream_time;
	/**
	 * Extra delay added to base_time to compensate for computing delays when setting
	 * elements to PLAYING
	 */
	uint64_t delay;
};

/**
 * @brief Initialize a pipeline
 *
 * Initializes the pipeline structure, including the base bin and message bus.
 *
 * @param self Pointer to the @ref mp_element to initialize as a pipeline
 */
void mp_pipeline_init(struct mp_element *self);

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_PIPELINE_H_ */
