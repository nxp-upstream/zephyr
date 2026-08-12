/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_pipeline.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_PIPELINE_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_PIPELINE_H_

/**
 * @defgroup mpipe_pipeline Pipelines
 * @ingroup mpipe_framework
 * @brief Top-level pipeline container and runtime control.
 * @{
 */

#include <stdint.h>

#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/mpipe/mpipe_bin.h>
#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_thread.h>

/**
 * @{
 */

/**
 * @brief struct mpipe structure
 *
 * Contains all the state and timing information needed to manage
 * a complete media processing pipeline.
 */
struct mpipe {
	/** Base bin container */
	struct mpipe_bin bin;
	/** Thread associated with the pipeline */
	struct mpipe_thread thread;
	/** The running time - total time spent in PLAYING state without being flushed */
	uint64_t stream_time;
	/**
	 * Extra delay added to base_time to compensate for computing delays when setting
	 * elements to PLAYING
	 */
	uint64_t delay;
	/** Number of sink elements in the pipeline (computed on READY->PAUSED) */
	uint32_t num_sinks;
	/** Number of EOS messages seen so far during the current run */
	atomic_t eos_count;
};

/**
 * @brief Initialize a pipeline
 *
 * Initializes the pipeline structure, including the base bin and message bus.
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a pipeline
 */
void mpipe_pipeline_init(struct mpipe_element *self);

/**
 * @brief Push a buffer downstream starting from a given source pad
 *
 * Walks downstream from an element's @p src_pad, calling each next element's chainfn
 * until a sink is reached, a chainfn fails, or output buffer is NULL
 *
 * On chainfn error the buffer is unreffed internally.
 *
 * @param src_pad Source pad to start pushing from (its peer's chainfn is first called)
 * @param buffer Buffer to push (ownership transferred)
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_push_buffer(struct mpipe_pad *src_pad, struct net_buf *buffer);

/**
 * @}
 */

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_PIPELINE_H_ */
