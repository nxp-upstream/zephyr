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
 * @brief Get clock identifier
 */
#define Z_CLOCK_DT_CLK_ID(node_id) _CONCAT(clk_dts_ord_, DT_DEP_ORD(node_id))

/**
 * @brief Expands to the name of a global clock object.
 *
 * Return the full name of a clock object symbol created by CLOCK_DT_DEFINE(),
 * using the `clk_id` provided by Z_CLOCK_DT_CLK_ID(). This is the name of the
 * global variable storing the clock structure
 *
 * It is meant to be used for declaring extern symbols pointing to clock objects
 * before using the CLOCK_GET macro to get the device object.
 *
 * @param clk_id Clock identifier.
 *
 * @return The full name of the clock object defined by clock definition
 * macros.
 */
#define CLOCK_NAME_GET(clk_id) _CONCAT(__clock_, clk_id)

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
#define CLOCK_DT_NAME_GET(node_id) CLOCK_NAME_GET(Z_CLOCK_DT_CLK_ID(node_id))

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
 * @brief Helper to get a clock dependency ordinal if the clock is referenced
 *
 * The build system will convert these dependency ordinals into clock object
 * references after the first link phase is completed
 * @param node_id Clock identifier
 */
#define Z_GET_CLOCK_DEP_ORD(node_id)                                           \
	IF_ENABLED(DT_CLOCK_USED(node_id),                                     \
		((const struct clk *)DT_DEP_ORD(node_id),))

/**
 * @brief Clock dependency array name
 * @param node_id Clock identifier
 */
#define CLOCK_DEPS_NAME(node_id) _CONCAT(Z_CLOCK_DT_CLK_ID(node_id), _deps)

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
	Z_CLOCK_BASE_DEFINE(node_id, Z_CLOCK_DT_CLK_ID(node_id), data,         \
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
 * @brief Define clock dependency array
 *
 * This macro defines a clock dependency array. The clock should
 * call this macro from its init macro, and can then get a reference to
 * the clock dependency array with `CLOCK_GET_DEPS`
 *
 * In the initial build, this array will expand to a list of clock ordinal
 * numbers that describe dependencies of the clock, like so:
 * @code{.c}
 *     const struct clk *const __weak clk_dts_ord_48_deps = {
 *         66,
 *         55,
 *         30,
 *     }
 * @endcode
 *
 * In the second pass of the build, gen_clock_deps.py will create a strong
 * symbol to override the weak one, with each ordinal number resolved to
 * the clock structure (or NULL, if no clock structure was defined in the
 * build). The final array will look like so:
 * @code{.c}
 *     const struct clk *const clk_dts_ord_48_deps = {
 *         __clock_clk_dts_ord_66,
 *         __clock_clk_dts_ord_55,
 *         NULL, // __clock_clk_dts_ord_30 was not defined in build
 *     }
 * @endcode
 * This multi-phase build is necessary so that the linker will optimize out
 * any clock object that are not referenced elsewhere in the build. This way,
 * a clock object will be discarded in the first link phase unless another
 * structure references it (such as a clock referencing its parent object)
 * @param node_id Clock identifier
 */
#define CLOCK_DEFINE_DEPS(node_id)                                             \
	const struct clk *const __weak CLOCK_DEPS_NAME(node_id)[] =            \
		{DT_FOREACH_SUPPORTED_NODE(node_id, Z_GET_CLOCK_DEP_ORD)};

/**
 * @brief Define clock dependency array for a clock instance
 *
 * This is equivalent to `CLOCK_DEFINE_DEPS(DT_DRV_INST(inst))`
 * @param inst Instance identifier
 */
#define CLOCK_INST_DEFINE_DEPS(inst) CLOCK_DEFINE_DEPS(DT_DRV_INST(inst))

/**
 * @brief Get clock dependency array
 *
 * This macro gets the c identifier for the clock dependency array,
 * declared with `CLOCK_DEFINE_DEPS`, which will contain
 * an array of pointers to the clock objects dependent on this clock.
 * @param node_id Clock identifier
 */
#define CLOCK_GET_DEPS(node_id) CLOCK_DEPS_NAME(node_id)


/**
 * @brief Get clock dependency array for a clock instance
 *
 * This is equivalent to `CLOCK_GET_DEPS(DT_DRV_INST(inst))`
 * @param inst Instance identifier
 */
#define CLOCK_INST_GET_DEPS(inst) CLOCK_GET_DEPS(DT_DRV_INST(inst))

/**
 * @brief Get count of clock dependencies
 *
 * This macro gets a count of the number of clock dependencies that exist
 * for a given clock
 * @param node_id Clock identifier
 */
#define CLOCK_NUM_DEPS(node_id)                                                \
	ARRAY_SIZE(CLOCK_DEPS_NAME(node_id))


/**
 * @brief Get count of clock instance dependencies
 *
 * This is equivalent to `CLOCK_NUM_DEPS(DT_DRV_INST(inst))`
 * @param inst Instance identifier
 */
#define CLOCK_INST_NUM_DEPS(inst) CLOCK_NUM_DEPS(DT_DRV_INST(inst))

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_ */
