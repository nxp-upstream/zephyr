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
#if defined(CONFIG_CLOCK_MGMT_SET_SET_RATE) || defined(__DOXYGEN__)
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(const struct clk *clk, uint32_t rate);
	/** Sets clock rate in Hz */
	int (*set_rate)(const struct clk *clk, uint32_t rate);
#endif
};

struct clock_mgmt_callback;

/**
 * @typedef clock_mgmt_callback_handler_t
 * @brief Define the application clock callback handler function signature
 *
 * @param user_data User data field set for callback
 */
typedef void (*clock_mgmt_callback_handler_t)(void *user_data);

/**
 * @brief Clock management callback structure
 *
 * Used to register a callback for clock change events in the driver consuming
 * the clock. As many callbacks as needed can be added as long as each of them
 * are unique pointers of struct clock_mgmt_callback.
 * Beware such pointers must not be allocated on the stack.
 *
 * Note: to help setting the callback, clock_mgmt_init_callback()
 */
struct clock_mgmt_callback {
	/** This is meant to be used in the driver and the user should not
	 * mess with it
	 */
	sys_snode_t node;

	/** Actual callback function being called when relevant */
	clock_mgmt_callback_handler_t handler;

	/** User data pointer */
	void *user_data;
};

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
	if (!(clk->api) || !(clk->api->configure)) {
		return -ENOSYS;
	}

	return clk->api->configure(clk, data);
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
 * @brief Helper to add or remove clock callback
 * @param callbacks A pointer to the original list of callbacks
 * @param callback A pointer of the callback to insert or remove from the list
 * @param set A boolean indicating insertion or removal of the callback
 */
static inline int clock_manage_callback(sys_slist_t *callbacks,
					struct clock_mgmt_callback *callback,
					bool set)
{
	__ASSERT(callback, "No callback!");
	__ASSERT(callback->handler, "No callback handler!");

	if (!sys_slist_is_empty(callbacks)) {
		if (!sys_slist_find_and_remove(callbacks, &callback->node)) {
			if (!set) {
				return -EINVAL;
			}
		}
	} else if (!set) {
		return -EINVAL;
	}

	if (set) {
		sys_slist_prepend(callbacks, &callback->node);
	}

	return 0;
}

/** @endcond */

/**
 * @brief Helper to initialize a struct clock_mgmt_callback properly
 * @param callback A valid callback structure pointer.
 * @param handler A valid handler function pointer.
 */
static inline void clock_init_callback(struct clock_mgmt_callback *callback,
				       clock_mgmt_callback_handler_t handler,
				       void *user_data)
{
	__ASSERT(callback, "Callback pointer should not be NULL");
	__ASSERT(handler, "Callback handler pointer should not be NULL");

	callback->handler = handler;
	callback->user_data = user_data;
}

/**
 * @brief Register a callback for clock rate change
 *
 * Registers a callback to fire when a clock's rate changes. The callback
 * will be called directly after the clock's rate has changed.
 * @param clk clock device to register callback for
 * @param cb clock callback structure pointer
 * @return -EINVAL if parameters are invalid
 * @return 0 on success
 */
static inline int clock_register_callback(const struct clk *clk,
					  struct clock_mgmt_callback *cb)
{
	if (!clk || !cb) {
		return -EINVAL;
	}

	/* Add callback to sys_slist_t */
	return clock_manage_callback(clk->callbacks, cb, true);
}


/**
 * @brief Unregister a callback for clock rate change
 *
 * Removes a callback for clock rate change
 * @param clk clock device to unregister callback for
 * @param cb clock callback structure pointer
 * @return -EINVAL if parameters are invalid
 * @return 0 on success
 */
static inline int clock_unregister_callback(const struct clk *clk,
					    struct clock_mgmt_callback *cb)
{
	if (!clk || !cb) {
		return -EINVAL;
	}

	/* Remove callback from sys_slist_t */
	return clock_manage_callback(clk->callbacks, cb, false);
}

/**
 * @brief Generic function to go through and fire callback for a clock object
 *
 * @param clk Clock object to fire callbacks for
 */
static inline void clock_fire_callbacks(const struct clk *clk)
{
	struct clock_mgmt_callback *cb, *tmp;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(clk->callbacks, cb, tmp, node) {
		__ASSERT(cb->handler, "No callback handler!");
		cb->handler(cb->user_data);
	}
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_ */
