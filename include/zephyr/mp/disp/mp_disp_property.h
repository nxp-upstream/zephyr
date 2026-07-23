/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Property identifiers for the MP disp plugin.
 */

#ifndef ZEPHYR_INCLUDE_MP_DISP_MP_DISP_PROPERTY_H_
#define ZEPHYR_INCLUDE_MP_DISP_MP_DISP_PROPERTY_H_

/**
 * @defgroup mp_disp_properties Properties
 * @ingroup mp_disp
 * @brief Display sink property identifiers.
 * @{
 */

#include <zephyr/mp/mp_property.h>

/**
 * @brief Display sink property identifiers.
 *
 * Extends the base sink properties defined in @ref mp_property.h.
 * Enumeration starts from @ref MP_PROP_SINK_LAST to avoid conflicts.
 */
enum {
	/** Display device property (const struct device *). */
	MP_PROP_DISP_SINK_DEVICE = MP_PROP_SINK_LAST,
};

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_DISP_MP_DISP_PROPERTY_H_ */
