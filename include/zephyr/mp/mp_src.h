/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_src.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_SRC_H_
#define ZEPHYR_INCLUDE_MP_MP_SRC_H_

/**
 * @defgroup mp_src Sources
 * @ingroup mp_framework
 * @brief Elements that generate data.
 *
 * @{
 */

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_pad.h>

/**
 * @brief Properties for a base source element
 *
 * Enumeration of properties that can be configured for base source
 */
enum mp_prop_src {
	/** Number of buffers that the source outputs before sending EOS */
	MP_PROP_SRC_NUM_BUFS,
	/** Last source property marker (for validation/iteration) */
	MP_PROP_SRC_LAST,
};

/**
 * @brief Base Source Element Structure
 *
 * The source element is responsible for generating data and pushing it downstream.
 * It contains a source pad for output.
 */
struct mp_src {
	/** Base element structure */
	struct mp_element element;
	/** Source pad for output data */
	struct mp_pad srcpad;
	/** Buffer pool for managing output buffers */
	struct mp_buffer_pool *pool;
	/**
	 * Number of buffers that the source outputs before sending EOS;
	 * 0 means will run forever
	 */
	uint32_t num_buffers;
	/** Set a given capability on the source pad */
	int (*set_caps)(struct mp_src *src, const struct mp_structure *caps);
	/** Decide buffer allocation strategy for the downstream peer */
	int (*decide_allocation)(struct mp_src *self, struct mp_dispatch *query);
};

/**
 * @brief Initialize a source element
 *
 * This function initializes the base source element structure, including
 * the source pad and default callbacks.
 *
 * @param self Pointer to the @ref mp_element to initialize as a source
 */
void mp_src_init(struct mp_element *self);

/**
 * @brief Change state function for base source element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived sources that override change_state must chain to this base function to
 * inherit that behavior.
 *
 * @param self Pointer to the @ref mp_element struct
 * @param transition Transition state, see @ref mp_state_change
 *
 * @return One of @ref mp_state_change_return
 */
enum mp_state_change_return mp_src_change_state(struct mp_element *self,
						enum mp_state_change transition);

/**
 * @brief Set property on source element
 *
 * @param obj Pointer to the @ref mp_object (source element)
 * @param key Property key identifier
 * @param val Pointer to the property value to set
 * @return 0 on success, negative errno on failure
 */
int mp_src_set_property(struct mp_object *obj, uint32_t key, const void *val);

/**
 * @brief Get property from source element
 *
 * @param obj Pointer to the @ref mp_object (source element)
 * @param key Property key identifier
 * @param val Pointer to store the retrieved property value
 * @return 0 on success, negative errno on failure
 */
int mp_src_get_property(struct mp_object *obj, uint32_t key, void *val);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_SRC_H_ */
