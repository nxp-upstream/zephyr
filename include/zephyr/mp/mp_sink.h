/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_sink.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_SINK_H_
#define ZEPHYR_INCLUDE_MP_MP_SINK_H_

/**
 * @defgroup mp_sink Sinks
 * @ingroup mp_framework
 * @brief Terminal elements that consume data from a pipeline.
 *
 * @{
 */

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_pad.h>

struct mp_dispatch;

/**
 * @brief Properties for a sink element
 *
 * Enumeration of properties that can be configured for base sink
 */
enum mp_prop_sink {
	/** Last sink property marker (for validation/iteration) */
	MP_PROP_SINK_LAST,
};

/**
 * @brief Sink Element Structure
 *
 * Represents a sink element in the media pipeline. Sink elements are terminal
 * elements that consume data from upstream elements through their sink pad.
 */
struct mp_sink {
	/** Base element structure */
	struct mp_element element;
	/** Input pad for receiving data */
	struct mp_pad sinkpad;
	/** Buffer pool */
	struct mp_buffer_pool *pool;
	/**
	 * @brief Set a given caps to the element
	 * @param sink Pointer to the sink element
	 * @param caps Capability to set
	 * @return 0 on success, negative errno on failure
	 */
	int (*set_caps)(struct mp_sink *sink, const struct mp_structure *caps);
	/**
	 * @brief Propose allocation strategy to the upstream peer
	 * @param self Pointer to the sink element
	 * @param query Allocation query to process
	 * @return 0 on success, negative errno on failure
	 */
	int (*propose_allocation)(struct mp_sink *self, struct mp_dispatch *query);
};

/**
 * @brief Initialize a sink element
 *
 * Initializes the base sink element structure, sets up the sink pad,
 * and configures default callbacks for query and event handling.
 *
 * @param self Pointer to the @ref mp_element to initialize as a sink
 */
void mp_sink_init(struct mp_element *self);

/**
 * @brief Change state function for the base sink element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived sinks that override change_state must chain to this base function to
 * inherit that behavior.
 *
 * @param self Pointer to the @ref mp_element
 * @param transition Transition state, see @ref mp_state_change
 *
 * @return One of @ref mp_state_change_return
 */
enum mp_state_change_return mp_sink_change_state(struct mp_element *self,
						 enum mp_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_SINK_H_ */
