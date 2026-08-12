/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_src.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_H_

/**
 * @defgroup mpipe_src Sources
 * @ingroup mpipe_framework
 * @brief Elements that generate data.
 *
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pad.h>

/**
 * @brief Properties for a base source element
 *
 * Enumeration of properties that can be configured for base source
 */
enum mpipe_prop_src {
	/** Number of buffers that the source outputs before sending EOS */
	MPIPE_PROP_SRC_NUM_BUFS,
	/** Last source property marker (for validation/iteration) */
	MPIPE_PROP_SRC_LAST,
};

/**
 * @brief Base Source Element Structure
 *
 * The source element is responsible for generating data and pushing it downstream.
 * It contains a source pad for output.
 */
struct mpipe_src {
	/** Base element structure */
	struct mpipe_element element;
	/** Source pad for output data */
	struct mpipe_pad src_pad;
	/** Buffer pool for managing output buffers */
	struct mpipe_buffer_pool *pool;
	/**
	 * Number of buffers that the source outputs before sending EOS;
	 * 0 means will run forever
	 */
	uint32_t num_buffers;
	/** Set a given capability on the source pad */
	int (*set_caps)(struct mpipe_src *src, const struct mpipe_structure *caps);
	/** Decide buffer allocation strategy for the downstream peer */
	int (*decide_allocation)(struct mpipe_src *self, struct mpipe_dispatch *query);
};

/**
 * @brief Initialize a source element
 *
 * This function initializes the base source element structure, including
 * the source pad and default callbacks.
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a source
 */
void mpipe_src_init(struct mpipe_element *self);

/**
 * @brief Change state function for base source element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived sources that override change_state must chain to this base function to
 * inherit that behavior.
 *
 * @param self Pointer to the @ref mpipe_element struct
 * @param transition Transition state, see @ref mpipe_state_change
 *
 * @return One of @ref mpipe_state_change_return
 */
enum mpipe_state_change_return mpipe_src_change_state(struct mpipe_element *self,
						      enum mpipe_state_change transition);

/**
 * @brief Set property on source element
 *
 * @param obj Pointer to the @ref mpipe_object (source element)
 * @param key Property key identifier
 * @param val Pointer to the property value to set
 * @return 0 on success, negative errno on failure
 */
int mpipe_src_set_property(struct mpipe_object *obj, uint32_t key, const void *val);

/**
 * @brief Get property from source element
 *
 * @param obj Pointer to the @ref mpipe_object (source element)
 * @param key Property key identifier
 * @param val Pointer to store the retrieved property value
 * @return 0 on success, negative errno on failure
 */
int mpipe_src_get_property(struct mpipe_object *obj, uint32_t key, void *val);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_SRC_H_ */
