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
	sys_slist_t callbacks;
	/** Address of private clock instance configuration information */
	const void *config;
	/** Address of private clock instance mutable data */
	void *data;
	/** Gets clock rate in Hz */
	int (*get_rate)(const struct clk *clk);
	/** Configure a clock with device specific data */
	int (*configure)(const struct clk *clk, void *data);
#ifdef CONFIG_CLOCK_MGMT_SET_SET_RATE
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(const struct clk *clk, uint32_t rate);
	/** Sets clock rate in Hz */
	int (*set_rate)(const struct clk *clk, uint32_t rate);
#endif
};

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
	/** Gets nearest rate clock can support, in Hz */
	int (*round_rate)(const struct clk *clk, uint32_t rate);
	/** Sets clock rate in Hz */
	int (*set_rate)(const struct clk *clk, uint32_t rate);
};

/**
 * @brief Clock Driver initialization structure
 */
struct clock_init {
	/** Clock initialization function */
	void (*init_fn)(const struct clk *clk);
	/** Parameter to pass to initialization function*/
	const struct clk *clk;
};

/**
 * @brief Expands to the name of a global clock object.
 *
 * Return the full name of a clock object symbol created by CLOCK_DT_DEFINE(),
 * using the `dev_id` provided by Z_DEVICE_DT_DEV_ID(). This is the name of the
 * global variable storing the clock structure
 *
 * It is meant to be used for declaring extern symbols pointing to clock objects
 * before using the CLOCK_GET macro to get the device object.
 *
 * @param dev_id Device identifier.
 *
 * @return The full name of the clock object defined by clock definition
 * macros.
 */
#define CLOCK_NAME_GET(dev_id) _CONCAT(__clock_, dev_id)

/**
 * @brief The name of the global clock object for @param node_id
 *
 * Returns the name of the global clock structure as a C identifier. The clock
 * must be allocated using CLOCK_DT_DEFINE() or CLOCK_DT_INST_DEFINE() for
 * this to work.
 *
 * @param node_id Devicetree node identifier
 *
 * @return The name of the clock object as a C identifier
 */
#define CLOCK_DT_NAME_GET(node_id) CLOCK_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))

/**
 * @brief Get a @ref clk reference from a clock devicetree node identifier.
 *
 * Returns a pointer to a clock object created from a devicetree node, if any
 * clock was allocated by a driver. If not such clock was allocated, this will
 * fail at linker time. If you get an error that looks like
 * `undefined reference to __device_dts_ord_<N>`, that is what happened.
 * Check to make sure your clock driver is being compiled,
 * usually by enabling the Kconfig options it requires.
 *
 * @param node_id A devicetree node identifier
 *
 * @return A pointer to the clock object created for that node
 */
#define CLOCK_DT_GET(node_id) (&CLOCK_DT_NAME_GET(node_id))

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
		.get_rate = (api)->get_rate,                                   \
		.configure = (api)->configure,                                 \
		IF_ENABLED(CONFIG_CLOCK_MGMT_SET_SET_RATE, (                   \
		.round_rate = (api)->round_rate,                               \
		.set_rate = (api)->set_rate,))                                 \
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
	const struct clk CLOCK_NAME_GET(clk_id) =                              \
		Z_CLOCK_INIT(&(CLOCK_NAME_GET(clk_id).callbacks), data,        \
			     config, api);

/**
 * @brief Obtain clock init entry name.
 *
 * @param init_id Init entry unique identifier.
 */
#define Z_CLOCK_INIT_NAME(init_id) _CONCAT(__clock_init_, CLOCK_NAME_GET(init_id))

/**
 * @brief Define the init entry for a clock.
 *
 * @param node_id Devicetree node id for the clock
 * @param dev_id Device identifier.
 * @param init_fn_ Clock init function.
 */
#define Z_CLOCK_INIT_ENTRY_DEFINE(node_id, dev_id, init_fn_)                   \
	struct clock_init Z_CLOCK_INIT_NAME(dev_id) = {                        \
		.init_fn = init_fn_,                                           \
		.clk = CLOCK_DT_GET(node_id),                                  \
	};

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
 * be run by the kernel during system initialization. Can be `NULL`. Note
 * that this function runs in the PRE_KERNEL_1 init phase.
 * @param data Pointer to the clock's private mutable data, which will be
 * stored in the @ref clk.data field
 * @param config Pointer to the clock's private constant data, which will be
 * stored in the @ref clk.config field
 * @param api Pointer to the clock's API structure.
 */

#define CLOCK_DT_DEFINE(node_id, init_fn, data, config, api, ...)              \
	Z_CLOCK_BASE_DEFINE(node_id, Z_DEVICE_DT_DEV_ID(node_id), data,        \
			    config, api)                                       \
	Z_CLOCK_INIT_ENTRY_DEFINE(node_id, Z_DEVICE_DT_DEV_ID(node_id), init_fn)

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

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_DRIVER_H_ */
