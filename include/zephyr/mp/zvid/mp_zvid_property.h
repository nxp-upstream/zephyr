/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp_zvid_properties
 * @brief Property identifiers for the zvid plugin.
 *
 * Extends the base source and transform property enumerations with
 * video-specific property keys.
 */

#ifndef ZEPHYR_INCLUDE_MP_ZVID_MP_ZVID_PROPERTY_H_
#define ZEPHYR_INCLUDE_MP_ZVID_MP_ZVID_PROPERTY_H_

/**
 * @defgroup mp_zvid_properties Properties
 * @ingroup mp_zvid
 * @brief Property identifiers for zvid elements.
 * @{
 */

#include <zephyr/sys/util.h>

#include <zephyr/mp/core/mp_property.h>

/**
 * @brief Zvid property identifiers.
 *
 * Starts from MAX(@ref PROP_SRC_LAST, @ref PROP_TRANSFORM_LAST) + 1 to
 * avoid conflicts with base source and transform properties.
 */
enum prop_zvid {
	/** Video device property */
	PROP_ZVID_DEVICE = MAX((int)PROP_SRC_LAST, (int)PROP_TRANSFORM_LAST) + 1,
	/** Crop selection target property */
	PROP_ZVID_CROP,
};

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_ZVID_MP_ZVID_PROPERTY_H_ */
