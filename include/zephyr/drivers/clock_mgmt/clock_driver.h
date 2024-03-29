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

#include <zephyr/device.h>
#include <zephyr/sys/slist.h>

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
 * @brief Runtime clock structure (in ROM) for each clock node
 */
struct clk {
	/** Callbacks to notify when clock is reconfigured */
	sys_slist_t callbacks
	/** Address of private clock instance configuration information */
	const void *config;
	/** Address of private clock instance mutable data */
	void *data;
	/** Gets clock rate in Hz */
	int (*get_rate)(struct clk*);
	/** Configure a clock with device specific data */
	int (*configure)(struct clk*, void*);
#ifdef CONFIG_CLOCK_MGMT_SET_SET_RATE
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(struct clk*, uint32_t);
	/** Sets clock rate in Hz */
	int (*set_rate)(struct clk*, uint32_t);
#endif
}

/**
 * @brief Clock Driver API
 *
 * Clock driver API function prototypes. A pointer to a structure of this
 * type should be passed to "CLOCK_DT_DEFINE" when defining the @ref clk
 */
struct clock_driver_api {
	/** Gets clock rate in Hz */
	int (*get_rate)(struct clk*);
	/** Configure a clock with device specific data */
	int (*configure)(struct clk*, void*);
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(struct clk*, uint32_t);
	/** Sets clock rate in Hz */
	int (*set_rate)(struct clk*, uint32_t);
};

/**
 * @brief Initializer for @ref clk.
 *
 * @param callbacks_ clock callback field to initialize
 * @param data_ Mutable data pointer for clock
 * @param config_ Constant configuration pointer for clock
 * @param api Pointer to the clock's API structure.
 */
#define Z_CLOCK_INIT(callbacks_, data_, config_, api)                          \
	{                                                                      \
		.callbacks = SYS_SLIST_STATIC_INIT(_callbacks),                \
		.data = data_,                                                 \
		.config = config_,                                             \
		.get_rate = api.get_rate,                                      \
		.configure = api.configure,                                    \
		IF_ENABLED(CONFIG_CLOCK_MGMT_SET_SET_RATE, (                   \
		.round_rate = api.round_rate,                                  \
		.set_rate = api.set_rate,))                                    \
	}

/**
 * @brief Define a @ref clk object
 *
 * Defines and initializes configuration and data fields of a @ref clk
 * object
 * @param node_id The devicetree node identifier.
 * @param clk_id clock identifier (used to name the defined @ref clk).
 * @param data Pointer to the clock's private mutable data, which will be
 * stored in the @ref clk.data field
 * @param config Pointer to the clock's private constant data, which will be
 * stored in the @ref clk.config field
 * @param api Pointer to the clock's API structure.
 */
#define Z_CLOCK_BASE_DEFINE(node_id, clk_id, data, config, api)                \
	const struct clk DEVICE_NAME_GET(clk_id) =                             \
		Z_CLOCK_INIT(&(DEVICE_NAME_GET(clk_id).callbacks), data, config)

/**
 * @brief Create a clock object from a devicetree node identifier and set it
 * up for boot time initialization.
 *
 * This macro defines a @ref clk that is automatically configured during
 * system initialization. The kernel will call @param init_fn during boot,
 * which allows the clock to setup any device data structures needed at
 * runtime (but should not ungate the clock). The global clock object's
 * name as a C identifier is derived from the node's dependency ordinal.
 * It is an error to call this macro on a node that has already been
 * defined with DEVICE_DT_DEFINE.
 *
 * Note that users should not directly reference clock objects, but instead
 * should use the clock management API. Clock objects are considered
 * internal to the clock subsystem.
 *
 * @param node_id The devicetree node identifier.
 * @param init_fn Pointer to the clock's initialization function, which will
 * be run by the kernel during system initialization. Can be `NULL`.
 * @param data Pointer to the clock's private mutable data, which will be
 * stored in the @ref clk.data field
 * @param config Pointer to the clock's private constant data, which will be
 * stored in the @ref clk.config field
 * @param api Pointer to the clock's API structure.
 */

#define CLOCK_DT_DEFINE(node_id, init_fn, data, config, api, ...)              \
	Z_CLOCK_BASE_DEFINE(node_id, Z_DEVICE_DT_DEV_ID(node_id), data,        \
			    config, api)                                       \
	Z_CLOCK_INIT_ENTRY_DEFINE(node_id, dev_id, init_fn)

/**
 * @brief Like CLOCK_DT_DEFINE(), but uses an instance of `DT_DRV_COMPAT`
 * compatible instead of a node identifier
 * @param inst Instance number. The `node_id` argument to CLOCK_DT_DEFINE is
 * set to `DT_DRV_INST(inst)`.
 * @param ... Other parameters as expected by CLOCK_DT_DEFINE().
 */
#define CLOCK_DT_INST_DEFINE(inst, ...)                                        \
	CLOCK_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_ */
