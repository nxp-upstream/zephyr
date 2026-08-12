/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stream parser base element.
 *
 * Provides a base class for elements that accumulate incoming data
 * into complete frames before pushing them downstream.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_PARSER_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_PARSER_H_

/**
 * @defgroup mpipe_parser Parsers
 * @ingroup mpipe_framework
 * @brief Base parser element that parses encoded streams into frames.
 *
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pad.h>

/**
 * @brief Base parser element structure.
 *
 * Extends @ref mpipe_element with sink and source pads, capability
 * negotiation, and allocation hooks for frame-reassembly elements.
 */
struct mpipe_parser {
	/** Base element */
	struct mpipe_element element;
	/** Sink pad to receive data */
	struct mpipe_pad sink_pad;
	/** Source pad to send data */
	struct mpipe_pad src_pad;
	/** @cond INTERNAL_HIDDEN */
	/** @endcond */
	/** Pointer to the output buffer pool */
	struct mpipe_buffer_pool *out_pool;

	/**
	 * @brief Set a capability on a pad.
	 *
	 * @param parser Pointer to the parser element.
	 * @param direction Pad direction (see @ref mpipe_pad_direction).
	 * @param caps Pointer to the capability to set.
	 *
	 * @return 0 on success, negative errno on failure
	 */
	int (*set_caps)(struct mpipe_parser *parser, enum mpipe_pad_direction direction,
			const struct mpipe_structure *caps);
	/**
	 * @brief Propose allocation parameters to upstream.
	 *
	 * @param parser Pointer to the parser element.
	 * @param query Allocation query (see @ref mpipe_dispatch).
	 *
	 * @return 0 on success, negative errno on failure
	 */
	int (*propose_allocation)(struct mpipe_parser *parser, struct mpipe_dispatch *query);
	/**
	 * @brief Decide allocation parameters for downstream.
	 *
	 * @param parser Pointer to the parser element.
	 * @param query Allocation query (see @ref mpipe_dispatch).
	 *
	 * @return 0 on success, negative errno on failure
	 */
	int (*decide_allocation)(struct mpipe_parser *parser, struct mpipe_dispatch *query);
};

/**
 * @brief Initialize a parser element.
 *
 * @param self Pointer to the @ref mpipe_element to initialize.
 */
int mpipe_parser_init(struct mpipe_parser *parser, uint8_t id);

/**
 * @brief Change state function for the base parser element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived parsers that override change_state must chain to this base function
 * to inherit that behavior.
 *
 * @param self Pointer to the @ref mpipe_element
 * @param transition Transition state, see @ref mpipe_state_change
 *
 * @return One of @ref mpipe_state_change_return
 */
enum mpipe_state_change_return mpipe_parser_change_state(struct mpipe_element *self,
							 enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_PARSER_H_ */
