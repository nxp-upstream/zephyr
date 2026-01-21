/*
 * SPDX-License-Identifier: Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup opamp_interface
 * @brief Main header file for OPAMP (Operational Amplifier) driver API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_OPAMP_H_
#define ZEPHYR_INCLUDE_DRIVERS_OPAMP_H_

/**
 * @brief Configure and control operational amplifiers.
 * @defgroup opamp_interface Operational amplifiers
 * @since 4.3
 * @version 0.1.0
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/dt-bindings/opamp/opamp.h>
#include <zephyr/device.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief OPAMP gain factors. */
enum opamp_gain {
	OPAMP_GAIN_1_7 = 0, /**< Gain factor: 1/7. */
	OPAMP_GAIN_1_3,     /**< Gain factor: 1/3. */
	OPAMP_GAIN_1,       /**< Gain factor: 1. */
	OPAMP_GAIN_5_3,     /**< Gain factor: 5/3. */
	OPAMP_GAIN_2,       /**< Gain factor: 2. */
	OPAMP_GAIN_11_5,    /**< Gain factor: 11/5. */
	OPAMP_GAIN_3,       /**< Gain factor: 3. */
	OPAMP_GAIN_4,       /**< Gain factor: 4. */
	OPAMP_GAIN_13_3,    /**< Gain factor: 13/3. */
	OPAMP_GAIN_7,       /**< Gain factor: 7. */
	OPAMP_GAIN_8,       /**< Gain factor: 8. */
	OPAMP_GAIN_15,      /**< Gain factor: 15. */
	OPAMP_GAIN_16,      /**< Gain factor: 16. */
	OPAMP_GAIN_31,      /**< Gain factor: 31. */
	OPAMP_GAIN_32,      /**< Gain factor: 32. */
	OPAMP_GAIN_33,      /**< Gain factor: 33. */
	OPAMP_GAIN_63,      /**< Gain factor: 63. */
	OPAMP_GAIN_64,      /**< Gain factor: 64. */
};

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal use only, skip these in public documentation.
 */

typedef int (*opamp_api_set_gain_t)(const struct device *dev, enum opamp_gain gain);

__subsystem struct opamp_driver_api {
	opamp_api_set_gain_t set_gain;
};

/** @endcond */

/**
 * @brief Set the OPAMP gain.
 *
 * @param[in] dev Device instance for the OPAMP driver.
 * @param gain Requested gain setting. Must be a valid member of enum opamp_gain.
 *
 * @retval 0 Success.
 * @return The result of the underlying driver call.
 */
__syscall int opamp_set_gain(const struct device *dev, enum opamp_gain gain);

static inline int z_impl_opamp_set_gain(const struct device *dev, enum opamp_gain gain)
{
	return DEVICE_API_GET(opamp, dev)->set_gain(dev, gain);
}

#ifdef __cplusplus
}
#endif

/** @} */

#include <zephyr/syscalls/opamp.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_OPAMP_H_ */
