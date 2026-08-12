/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_sink.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_H_

/**
 * @defgroup mpipe_sink Sinks
 * @ingroup mpipe_framework
 * @brief Terminal elements that consume data from a pipeline.
 *
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pad.h>

struct mpipe_dispatch;

/**
 * @brief Properties for a sink element
 *
 * Enumeration of properties that can be configured for base sink
 */
enum mpipe_prop_sink {
	/** Last sink property marker (for validation/iteration) */
	MPIPE_PROP_SINK_LAST,
};

/**
 * @brief Sink Element Structure
 *
 * Represents a sink element in the media pipeline. Sink elements are terminal
 * elements that consume data from upstream elements through their sink pad.
 */
struct mpipe_sink {
	/** Base element structure */
	struct mpipe_element element;
	/** Input pad for receiving data */
	struct mpipe_pad sink_pad;
	/** Buffer pool */
	struct mpipe_buffer_pool *pool;
	/**
	 * @brief Set a given caps to the element
	 * @param sink Pointer to the sink element
	 * @param caps Capability to set
	 * @return 0 on success, negative errno on failure
	 */
	int (*set_caps)(struct mpipe_sink *sink, const struct mpipe_structure *caps);
	/**
	 * @brief Propose allocation strategy to the upstream peer
	 * @param self Pointer to the sink element
	 * @param query Allocation query to process
	 * @return 0 on success, negative errno on failure
	 */
	int (*propose_allocation)(struct mpipe_sink *self, struct mpipe_dispatch *query);
};

/**
 * @brief Initialize a sink element
 *
 * Initializes the base sink element structure, sets up the sink pad,
 * and configures default callbacks for query and event handling.
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a sink
 */
void mpipe_sink_init(struct mpipe_element *self);

/**
 * @brief Change state function for the base sink element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived sinks that override change_state must chain to this base function to
 * inherit that behavior.
 *
 * @param self Pointer to the @ref mpipe_element
 * @param transition Transition state, see @ref mpipe_state_change
 *
 * @return One of @ref mpipe_state_change_return
 */
enum mpipe_state_change_return mpipe_sink_change_state(struct mpipe_element *self,
						       enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_SINK_H_ */
