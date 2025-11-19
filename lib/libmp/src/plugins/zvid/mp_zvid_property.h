/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for zvid plugin properties.
 */

#ifndef __MP_ZVID_PROPS_H__
#define __MP_ZVID_PROPS_H__

#include <src/core/mp_property.h>

/**
 * @brief Zvid Transform Property Identifiers
 *
 * Defined property identifiers specific to the zvid transform element. These
 * properties extend the base transform properties defined in @ref prop_transform.
 *
 * The enumeration starts from @ref PROP_TRANSFORM_LAST + 1 to ensure no
 * conflicts with base transform properties.
 */
enum prop_zvid_transform {
	/** Video device property identifier */
	PROP_ZVID_TRANSFORM_DEVICE = PROP_TRANSFORM_LAST + 1,
};

/**
 * @brief Zvid Src Property Identifiers
 *
 * Defined property identifiers specific to the zvid src element. These
 * properties extend the base src properties defined in @ref prop_src.
 *
 * The enumeration starts from @ref PROP_SRC_LAST + 1 to ensure no
 * conflicts with base src properties.
 */
enum prop_zvid_src {
	/** Video device property identifier */
	PROP_ZVID_SRC_DEVICE = PROP_SRC_LAST + 1,
};

#endif /* __MP_ZVID_PROPS_H__ */
