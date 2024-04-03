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
 * @brief Notify children of a given clock about a reconfiguration event
 *
 * Calls `notify` API for all children of a given clock
 * @param clk Clock object to fire notifications for
 */
static inline void clock_notify_children(const struct clk *clk)
{
	const struct clk *child = clk->children;

	for (uint8_t i = 0; i < clk->child_count; i++) {
		/* Fire child's clock callback */
		clock_notify(child, clk);
		child++;
	}
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
 * Configure a clock device using hardware specific data
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

	/* Notify children of rate change */
	clock_notify_children(clk);
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


/** @cond INTERNAL_HIDDEN */

/**
 * @brief Name of clock notification list section
 *
 * Defines the name of the clock notification section. This must match the
 * section name declared by the linker for a given clock
 * @param node_id: Devicetree node identifier for clock this section must target
 */
#define Z_CLOCK_CB_SECT_NAME(node_id)                                          \
	_CONCAT(Z_DEVICE_DT_DEV_ID(node_id), _clock_cb)

/**
 * @brief Name of clock notification registration variable
 *
 * @param clk_id Devicetree node ID of clock device to register for a
 * notification
 * @param parent_id Devicetree node ID of parent clock wishes to receive
 * notification from
 */
#define Z_CLOCK_CB_REG_NAME(clk_id, parent_id)                                 \
	_CONCAT(_CONCAT(Z_DEVICE_DT_DEV_ID(clk_id), _reg_),                    \
		__COUNTER__)

/** @endcond */

/**
 * @brief Register for a clock notification
 *
 * Registers a clock for a configuration notification. This macro causes the
 * clock implemented by @p clk_id to be registered for a callback from the clock
 * implementing @p parent, such that whenever the @p parent clock
 * is reconfigured the `notify` API will be called on the @p clk clock.
 * @param clk_id Devicetree node ID of clock device to register for a
 * notification
 * @param parent_id Devicetree node ID of parent clock wishes to receive
 * notification from
 */
#define CLOCK_NOTIFY_REGISTER(clk_id, parent_id)                               \
	const struct clk Z_GENERIC_SECTION(Z_CLOCK_CB_SECT_NAME(parent_id))    \
		*Z_CLOCK_CB_REG_NAME(clk_id, parent_id) =                      \
		CLOCK_DT_GET(parent_id)


/**
 * @brief Register a clock instance for a notification
 *
 * Helper to register a clock instance for a notification Equivalent to
 * `CLOCK_NOTIFY_REGISTER(DT_DRV_INST(inst), parent_id)`.
 * @param inst Instance ID of clock device to register for a notification
 * @param parent_id Devicetree node ID of parent clock wishes to receive
 * notification from
 */
#define CLOCK_NOTIFY_REGISTER_INST(inst, parent_id)                            \
	CLOCK_NOTIFY_REGISTER(DT_DRV_INST(inst), parent_id)

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_ */
