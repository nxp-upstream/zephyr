/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Caps filter element.
 *
 * This element does not modify data, but used to enforce limitations on the data format.
 *
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_BASE_MPIPE_CAPS_FILTER_H_
#define ZEPHYR_INCLUDE_MPIPE_BASE_MPIPE_CAPS_FILTER_H_

/**
 * @defgroup mpipe_base base
 * @ingroup mpipe_plugins
 * @brief Base plugin elements shared across Multimedia Pipeline graphs.
 */

/**
 * @defgroup mpipe_caps_filter Caps Filters
 * @ingroup mpipe_base
 * @brief Transform elements that constrain negotiated capabilities.
 * @{
 */

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief Caps filter Property Identifiers
 *
 * Defined property identifiers specific to the caps_filter element. These
 * properties extend the base transform properties defined in @ref mpipe_prop_transform.
 *
 * The enumeration starts from MPIPE_PROP_TRANSFORM_LAST to ensure no
 * conflicts with base transform properties.
 */
enum {
	/** Caps ID property */
	MPIPE_PROP_BASE_CAPS_FILTER_CAPS = MPIPE_PROP_TRANSFORM_LAST,
};

/**
 * @brief Caps filter Element Structure
 *
 */
struct mpipe_caps_filter {
	/** Base transform element */
	struct mpipe_transform transform;
	/** Upstream source pad that the sink pad was linked to */
	struct mpipe_pad *saved_sink_peer;
	/** Downstream sink pad that the source pad was linked to */
	struct mpipe_pad *saved_src_peer;
	/** Configured filter, re-applied to the pads on PAUSED -> READY */
	struct mpipe_structure filter_caps;
};

/**
 * @brief Initialize a caps filter element
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a caps filter
 */
int mpipe_caps_filter_init(struct mpipe_caps_filter *caps_filter, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_BASE_MPIPE_CAPS_FILTER_H_ */
