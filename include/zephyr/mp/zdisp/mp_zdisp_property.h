/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Property identifiers for the MP zdisp plugin.
 */

#ifndef ZEPHYR_INCLUDE_MP_ZDISP_MP_ZDISP_PROPERTY_H_
#define ZEPHYR_INCLUDE_MP_ZDISP_MP_ZDISP_PROPERTY_H_

/**
 * @defgroup mp_zdisp_properties Properties
 * @ingroup mp_zdisp
 * @brief Display sink property identifiers.
 * @{
 */

#include <zephyr/mp/core/mp_property.h>

/**
 * @brief Display sink property identifiers.
 *
 * Extends the base sink properties defined in @ref mp_property.h.
 * Enumeration starts from @ref PROP_SINK_LAST + 1 to avoid conflicts.
 */
enum {
	/** Display device property (const struct device *). */
	PROP_ZDISP_SINK_DEVICE = PROP_SINK_LAST + 1,
};

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_ZDISP_MP_ZDISP_PROPERTY_H_ */
