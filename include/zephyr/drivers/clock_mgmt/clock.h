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

#include <zephyr/sys/slist.h>
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
	/** list of clock children */
	const struct clk *children;
	/** Length of clock children list */
	const uint32_t child_count;
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
 * @param children_ Array of clock children structures
 * @param child_count_ Number of children of this clock
 */
#define Z_CLOCK_INIT(data_, config_, api_, children_, child_count_)            \
	{                                                                      \
		.data = data_,                                                 \
		.config = config_,                                             \
		.api = api_,                                                   \
		.children = (const struct clk *)children_,                     \
		.child_count = (uint32_t)child_count_,                         \
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
	extern const char _CONCAT(clk_id, _clock_cb_start);                    \
	extern const char _CONCAT(clk_id, _clock_cb_size);                     \
	const struct clk CLOCK_NAME_GET(clk_id) =                              \
		Z_CLOCK_INIT(data, config, api,                                \
			&_CONCAT(clk_id, _clock_cb_start),                     \
			&_CONCAT(clk_id, _clock_cb_size));

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

/** @endcond */

/**
 * @brief Create a clock object from a devicetree node identifier and set it
 * up for boot time initialization.
 *
 * This macro defines a @ref clk. The global clock object's
 * name as a C identifier is derived from the node's dependency ordinal.
 * It is an error to call this macro on a node that has already been
 * defined with DEVICE_DT_DEFINE.
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
 * @brief Call @p fn on all clock nodes with compatible @p compat that
 *        are referenced within the devicetree
 *
 * This macro expands to:
 *
 *     fn(node_id_1) fn(node_id_2) ... fn(node_id_n)
 *
 * where each `node_id_<i>` is a node identifier for some node with
 * compatible @p compat that is referenced within the devicetree. Whitespace is
 * added between expansions as shown above.
 *
 * A clock is considered "referenced" if an node with status `okay` references
 * the clock node's phandle within its `clock-outputs` or `clock-state-<n>`
 * clock properties. If a clock node is referenced, all the nodes which it
 * references or is a child of will also be considered referenced. This applies
 * recursively.
 *
 * @note Although this macro has many of the same semantics as
 * @ref DT_FOREACH_STATUS_OKAY, it will only call @p fn for clocks
 * that are referenced in the devicetree, which will result in @p fn
 * only being called for clock nodes that can be used within the clock
 * management framework
 *
 * Example devicetree fragment:
 *
 * @code{.dts}
 *     a {
 *             compatible = "vnd,clock";
 *             status = "okay";
 *             foobar = "DEV_A";
 *     };
 *
 *     b {
 *             compatible = "vnd,clock";
 *             status = "okay";
 *             foobar = "DEV_B";
 *     };
 *
 *     c {
 *             compatible = "vnd,clock";
 *             status = "disabled";
 *             foobar = "DEV_C";
 *     };
 * @endcode
 *
 * Example usage:
 *
 * @code{.c}
 *     #define MY_FN(node) DT_PROP(node, foobar),
 *
 *     DT_FOREACH_CLK_REFERENCED(vnd_clock, MY_FN)
 * @endcode
 *
 * This expands to either:
 *
 *     "DEV_A", "DEV_B",
 *
 * or this:
 *
 *     "DEV_B", "DEV_A",

 *
 * No guarantees are made about the order that a and b appear in the
 * expansion.
 *
 * Note that @p fn is responsible for adding commas, semicolons, or
 * other separators or terminators.
 *
 * @param compat lowercase-and-underscores clock devicetree compatible
 * @param fn Macro to call for each referenced clock node. Must accept a
 *           node_id as its only parameter.
 */
#define DT_FOREACH_CLK_REFERENCED(compat, fn)                                  \
	IF_ENABLED(UTIL_CAT(DT_CLOCK_HAS_USED_, DT_DRV_COMPAT),                \
		    DT_CAT(DT_FOREACH_CLOCK_USED, compat)(fn))                 \

/**
 * @brief Call @p fn on all clock nodes with compatible `DT_DRV_COMPAT` that
 *        are referenced within the devicetree
 *
 * This macro calls `fn(inst)` on each `inst` number that refers to a clock node
 * that is referenced within the devicetree. Whitespace is added between
 * invocations.
 *
 * A clock is considered "referenced" if an node with status `okay` references
 * the clock node's phandle within its `clock-outputs` or `clock-state-<n>`
 * clock properties. If a clock node is referenced, all the nodes which it
 * references or is a child of will also be considered referenced. This applies
 * recursively.
 *
 * @note Although this macro has many of the same semantics as
 * @ref DT_INST_FOREACH_STATUS_OKAY, it will only call @p fn for clocks
 * that are referenced in the devicetree, which will result in @p fn
 * only being called for clock nodes that can be used within the clock
 * management framework
 *
 * Example devicetree fragment:
 *
 * @code{.dts}
 *     a {
 *             compatible = "vnd,clock";
 *             status = "okay";
 *             foobar = "DEV_A";
 *     };
 *
 *     b {
 *             compatible = "vnd,clock";
 *             status = "okay";
 *             foobar = "DEV_B";
 *     };
 *
 *     c {
 *             compatible = "vnd,clock";
 *             status = "disabled";
 *             foobar = "DEV_C";
 *     };
 * @endcode
 *
 * Example usage:
 *
 * @code{.c}
 *     #define DT_DRV_COMPAT vnd_clock
 *     #define MY_FN(inst) DT_INST_PROP(inst, foobar),
 *
 *     DT_INST_FOREACH_CLK_REFERENCED(MY_FN)
 * @endcode
 *
 * This expands to:
 *
 * @code{.c}
 *     MY_FN(0) MY_FN(1)
 * @endcode
 *
 * and from there, to either this:
 *
 *     "DEV_A", "DEV_B",
 *
 * or this:
 *
 *     "DEV_B", "DEV_A",
 *
 * No guarantees are made about the order that a and b appear in the
 * expansion.
 *
 * Note that @p fn is responsible for adding commas, semicolons, or
 * other separators or terminators.
 *
 * Clock drivers should use this macro whenever possible to instantiate
 * a struct clk for each referenced clock in the devicetree of the clock's
 * compatible `DT_DRV_COMPAT`
 *
 * @param fn Macro to call on each enabled node. Must accept an instance
 *           number as its only parameter
 */
#define DT_INST_FOREACH_CLK_REFERENCED(fn)                                     \
	IF_ENABLED(UTIL_CAT(DT_CLOCK_HAS_USED_, DT_DRV_COMPAT),                \
		    (UTIL_CAT(DT_FOREACH_CLOCK_USED_INST_, DT_DRV_COMPAT)(fn)))

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_ */
