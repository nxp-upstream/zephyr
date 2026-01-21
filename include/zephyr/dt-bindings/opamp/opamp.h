/*
 * SPDX-License-Identifier: Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree constants for OPAMP.
 * @ingroup opamp_dt
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_OPAMP_OPAMP_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_OPAMP_OPAMP_H_

/**
 * @brief Devicetree options for configuring operational amplifiers.
 * @defgroup opamp_dt OPAMP DT Options
 * @ingroup opamp_interface
 * @{
 */

/** @brief OPAMP functional modes. */
enum opamp_functional_mode {
	OPAMP_FUNCTIONAL_MODE_DIFFERENTIAL = 0, /**< Differential amplifier mode. */
	OPAMP_FUNCTIONAL_MODE_INVERTING,        /**< Inverting amplifier mode. */
	OPAMP_FUNCTIONAL_MODE_NON_INVERTING,    /**< Non-inverting amplifier mode. */
	OPAMP_FUNCTIONAL_MODE_FOLLOWER,         /**< Voltage follower mode. */
	/**
	 * Standalone mode.
	 *
	 * Gain is set by external resistors. Calls that attempt to set the gain may
	 * be ignored in this mode.
	 */
	OPAMP_FUNCTIONAL_MODE_STANDALONE,
};

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_OPAMP_OPAMP_H_ */
