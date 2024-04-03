/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * Internal APIs for clock management drivers
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_

#include <zephyr/sys/slist.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_mgmt/clock.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clock Driver Interface
 * @defgroup clock_driver_interface Clock Driver Interface
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Clock Driver API
 *
 * Clock driver API function prototypes. A pointer to a structure of this
 * type should be passed to "CLOCK_DT_DEFINE" when defining the @ref clk
 */
struct clock_driver_api {
	/** Gets clock rate in Hz */
	int (*get_rate)(const struct clk *clk);
	/** Configure a clock with device specific data */
	int (*configure)(const struct clk *clk, void *data);
	/** Notify a clock that a parent has been reconfigured */
	int (*notify)(const struct clk *clk, const struct clk *parent);
#if defined(CONFIG_CLOCK_MGMT_SET_SET_RATE) || defined(__DOXYGEN__)
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(const struct clk *clk, uint32_t rate);
	/** Sets clock rate in Hz */
	int (*set_rate)(const struct clk *clk, uint32_t rate);
#endif
};

/**
 * @brief Notify clock of parent reconfiguration
 *
 * Notifies a clock its parent was reconfigured
 * @param clk Clock object to notify of reconfiguration
 * @param parent Parent clock device that was reconfigured
 * @return -ENOSYS if clock does not implement notify_children API
 * @return negative errno for other error notifying clock
 * @return 0 on success
 */
static inline int clock_notify(const struct clk *clk, const struct clk *parent)
{
	if (!(clk->api) || !(clk->api->notify)) {
		return -ENOSYS;
	}

	return clk->api->notify(clk, parent);
}

/**
 * @brief Get rate of a clock
 *
 * Gets the rate of a clock, in Hz. A rate of zero indicates the clock is
 * active or powered down.
 * @param clk clock device to read rate from
 * @return -ENOSYS if clock does not implement get_rate API
 * @return -EIO if clock could not be read
 * @return negative errno for other error reading clock rate
 * @return frequency of clock output in HZ
 */
static inline int clock_get_rate(const struct clk *clk)
{
	if (!(clk->api) || !(clk->api->get_rate)) {
		return -ENOSYS;
	}

	return clk->api->get_rate(clk);
}

/**
 * @brief Configure a clock
 *
 * Configure a clock device using hardware specific data. This will also
 * trigger a reconfiguration notification for any consumers of the clock
 * @param clk clock device to configure
 * @param data hardware specific clock configuration data
 * @return -ENOSYS if clock does not implement configure API
 * @return -EIO if clock could not be configured
 * @return negative errno for other error configuring clock
 * @return 0 on successful clock configuration
 */
static inline int clock_configure(const struct clk *clk, void *data)
{
	int ret;

	if (!(clk->api) || !(clk->api->configure)) {
		return -ENOSYS;
	}

	ret = clk->api->configure(clk, data);
	if (ret < 0) {
		return ret;
	}
}


#if defined(CONFIG_CLOCK_MGMT_SET_SET_RATE) || defined(__DOXYGEN__)

/**
 * @brief Get nearest rate a clock can support
 *
 * Returns the actual rate that this clock would produce if `clock_set_rate`
 * was called with the requested frequency.
 * @param clk clock device to query
 * @param req_rate: Requested clock rate, in Hz
 * @return -ENOTSUP if API is not supported
 * @return -ENOSYS if clock does not implement round_rate API
 * @return -EIO if clock could not be queried
 * @return negative errno for other error calculating rate
 * @return rate clock would produce (in Hz) on success
 */
static inline int clock_round_rate(const struct clk *clk, uint32_t req_rate)
{
	if (!(clk->api) || !(clk->api->round_rate)) {
		return -ENOSYS;
	}

	return clk->api->round_rate(clk, req_rate);
}

/**
 * @brief Set a clock rate
 *
 * Sets a clock to the nearest frequency to the requested rate
 * @param clk clock device to set rate for
 * @param rate: rate to configure clock for, in Hz
 * @return -ENOTSUP if API is not supported
 * @return -ENOSYS if clock does not implement set_rate API
 * @return -EIO if clock rate could not be set
 * @return negative errno for other error setting rate
 * @return rate clock is set to produce (in Hz) on success
 */
static inline int clock_set_rate(const struct clk *clk, uint32_t rate)
{
	if (!(clk->api) || !(clk->api->set_rate)) {
		return -ENOSYS;
	}

	return clk->api->set_rate(clk, rate);
}

#else /* if !defined(CONFIG_CLOCK_MGMT_SET_SET_RATE) */

/* Stub functions to indicate set_rate and round_rate are not supported */
static inline int clock_round_rate(const struct clk *clk, uint32_t req_rate)
{
	return -ENOTSUP;
}

static inline int clock_set_rate(const struct clk *clk, uint32_t rate)
{
	return -ENOTSUP;
}

#endif /* defined(CONFIG_CLOCK_MGMT_SET_SET_RATE) || defined(__DOXYGEN__) */


#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_ */
