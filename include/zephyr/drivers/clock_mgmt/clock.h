/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * Internal APIs for clock management drivers
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_

#include <zephyr/devicetree/clock_mgmt.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clock Management Model
 * @defgroup clock_model Clock device model
 * @{
 */

/* Forward declaration of clock driver API struct */
struct clock_driver_api;

/**
 * @brief Runtime clock structure (in ROM) for each clock node
 */
struct clk {
	/** Address of private clock instance configuration information */
	const void *config;
	/** Address of private clock instance mutable data */
	void *data;
	/** API pointer for clock node */
	const struct clock_driver_api *api;
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

/** @cond INTERNAL_HIDDEN */

/**
 * @brief Initializer for @ref clk.
 *
 * @param data_ Mutable data pointer for clock
 * @param config_ Constant configuration pointer for clock
 * @param api_ Pointer to the clock's API structure.
 */
#define Z_CLOCK_INIT(data_, config_, api_)                                     \
	{                                                                      \
		.data = data_,                                                 \
		.config = config_,                                             \
		.api = api_,                                                   \
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
		Z_CLOCK_INIT(data, config, api);

/**
 * @brief Declare a clock for each used clock node in devicetree
 *
 * @note Unused nodes should not result in clocks, so not predeclaring these
 * keeps drivers honest.
 *
 * This is only "maybe" a clock because some nodes have status "okay", but
 * don't have a corresponding @ref clk allocated. There's no way to figure
 * that out until after we've built the zephyr image, though.
 * @param node_id Devicetree node identifier
 */
#define Z_MAYBE_CLOCK_DECLARE_INTERNAL(node_id)                                \
	extern const struct clk CLOCK_DT_NAME_GET(node_id);

DT_FOREACH_CLOCK_USED(Z_MAYBE_CLOCK_DECLARE_INTERNAL)

/**
 * @brief Helper to get a clock object if the clock is referenced
 * @param node_id Clock identifier
 */
#define Z_GET_CLOCK_IF_ENABLED(node_id)                                        \
	IF_ENABLED(DT_CLOCK_USED(node_id), (CLOCK_DT_GET(node_id),))

/** @endcond */

/**
 * @brief Create a clock object from a devicetree node identifier and set it
 * up for boot time initialization.
 *
 * This macro defines a @ref clk. The global clock object's
 * name as a C identifier is derived from the node's dependency ordinal.
 *
 * Note that users should not directly reference clock objects, but instead
 * should use the clock management API. Clock objects are considered
 * internal to the clock subsystem.
 *
 * @param node_id The devicetree node identifier.
 * @param data Pointer to the clock's private mutable data, which will be
 * stored in the @ref clk.data field
 * @param config Pointer to the clock's private constant data, which will be
 * stored in the @ref clk.config field
 * @param api Pointer to the clock's API structure.
 */

#define CLOCK_DT_DEFINE(node_id, data, config, api, ...)                       \
	Z_CLOCK_BASE_DEFINE(node_id, Z_DEVICE_DT_DEV_ID(node_id), data,        \
			    config, api)

/**
 * @brief Like CLOCK_DT_DEFINE(), but uses an instance of `DT_DRV_COMPAT`
 * compatible instead of a node identifier
 * @param inst Instance number. The `node_id` argument to CLOCK_DT_DEFINE is
 * set to `DT_DRV_INST(inst)`.
 * @param ... Other parameters as expected by CLOCK_DT_DEFINE().
 */
#define CLOCK_DT_INST_DEFINE(inst, ...)                                        \
	CLOCK_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

/**
 * @brief Get clock objects for all dependencies of a given clock
 *
 * Gets clock objects for all referenced dependencies of a given clock.
 * This macro will expand to a C array of clock references, which can
 * be useful for initializing a list of clock dependencies.
 * Example devicetree:
 * @code{.dts}
 *     parent_clk: parent-clk {
 *             child_clk: child-clk {};
 *     };
 *     clk_mux: clk-mux {
 *             vnd,inputs = <&parent_clk>;
 *     };
 * @endcode
 *
 * Example usage:
 *
 * @code{.c}
 *     const struct clk *clocks[] = CLOCK_GET_DEPS(DT_NODELABEL(parent_clk))
 * @endcode
 *
 * This is equivalent to the following:
 *     const struct clk *clocks[] = {CLOCK_DT_GET(DT_NODELABEL(child_clk)),
 *                                   CLOCK_DT_GET(DT_NODELABEL(clk_mux))}
 *
 * @param node_id Clock identifier
 */
#define CLOCK_GET_DEPS(node_id)                                                \
	{DT_FOREACH_SUPPORTED_NODE(node_id, Z_GET_CLOCK_IF_ENABLED)}


/**
 * @brief Get clock objects for all dependencies of a given clock instance
 *
 * This is equivalent to `CLOCK_GET_DEPS(DT_DRV_INST(inst))`
 * @param inst Instance identifier
 */
#define CLOCK_INST_GET_DEPS(inst) CLOCK_GET_DEPS(DT_DRV_INST(inst))

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_ */
