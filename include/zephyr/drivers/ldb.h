/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup ldb_interface
 * @brief Main header file for LDB (LVDS Display Bridge) driver API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LDB_H_
#define ZEPHYR_INCLUDE_DRIVERS_LDB_H_

/**
 * @brief Interfaces for LDB (LVDS Display Bridge).
 * @defgroup ldb_interface LDB
 * @since 4.5
 * @version 1.0.0
 * @ingroup display_interface
 * @{
 */

#include <errno.h>
#include <sys/types.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name LDB mode flags.
 * @{
 */

/**
 * @brief Enable dual-panel / split mode.
 *
 * When enabled, the bridge may:
 * - Enable split/dual mode in the LDB controller,
 * - Enable additional PHY options such as PHY_DIV,
 * depending on the underlying hardware implementation.
 */
#define LDB_MODE_DUAL_PANEL BIT(0)

/** @} */

/**
 * @brief LVDS bit mapping selection.
 *
 * This selects how pixel bits are mapped onto LVDS lanes.
 * Typical hardware uses a bit like CH0_BIT_MAPPING:
 * - 0b: SPWG standard
 * - 1b: JEIDA standard
 */
enum ldb_bit_mapping {
	/** Use SPWG standard bit mapping. */
	LDB_BIT_MAPPING_SPWG = 0,
	/** Use JEIDA standard bit mapping. */
	LDB_BIT_MAPPING_JEIDA = 1,
};

/**
 * @brief LDB link configuration.
 *
 * This structure describes how to configure the LDB output link.
 */
struct ldb_link_cfg {
	/** Input selection index (typically 0 or 1). */
	uint8_t input;

	/** LVDS bit mapping selection (SPWG/JEIDA). */
	enum ldb_bit_mapping bit_mapping;

	/** Mode flags (LDB_MODE_*). */
	uint32_t mode_flags;
};

/** LDB bridge driver API. */
__subsystem struct ldb_driver_api {
	int (*configure)(const struct device *dev, const struct ldb_link_cfg *cfg);
	int (*enable)(const struct device *dev);
	int (*disable)(const struct device *dev);
};

/**
 * @brief Configure the LDB link.
 *
 * @param dev LDB bridge device.
 * @param cfg LDB link configuration.
 *
 * @return 0 on success, negative on error
 */
static inline int ldb_configure(const struct device *dev, const struct ldb_link_cfg *cfg)
{
	const struct ldb_driver_api *api = DEVICE_API_GET(ldb, dev);

	if (api->configure == NULL) {
		return -ENOSYS;
	}

	return api->configure(dev, cfg);
}

/**
 * @brief Enable the LDB output link/PHY.
 *
 * @param dev LDB bridge device.
 *
 * @return 0 on success, negative on error
 */
static inline int ldb_enable(const struct device *dev)
{
	return DEVICE_API_GET(ldb, dev)->enable(dev);
}

/**
 * @brief Disable the LDB output link/PHY.
 *
 * @param dev LDB bridge device.
 *
 * @return 0 on success, negative on error
 */
static inline int ldb_disable(const struct device *dev)
{
	const struct ldb_driver_api *api = DEVICE_API_GET(ldb, dev);

	if (api->disable == NULL) {
		return -ENOSYS;
	}

	return api->disable(dev);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_LDB_H_ */
