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
#include <zephyr/sys/iterable_sections.h>
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
 * @brief Type used to represent a "handle" for a device.
 *
 * Every @ref clk has an associated handle. You can get a pointer to a
 * @ref clk from its handle but the handle uses less space
 * than a pointer. The clock.h API uses handles to store lists of clocks
 * in a compact manner
 *
 * The extreme negative value has special significance (signalling the end
 * of a clock list)
 *
 * @see clk_from_handle()
 */
typedef int16_t clock_handle_t;

/** @brief Flag value used to identify the end of a clock list. */
#define CLOCK_LIST_END INT16_MIN

/**
 * @brief Runtime clock structure (in ROM) for each clock node
 */
struct clk {
	/** Children nodes of the clock */
	const clock_handle_t *children;
	/** Pointer to private clock hardware data. May be in ROM or RAM. */
	void *hw_data;
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
 * @param children_ Children of this clock
 * @param hw_data Pointer to the clock's private data
 * @param api_ Pointer to the clock's API structure.
 */
#define Z_CLOCK_INIT(children_, hw_data_, api_)                                \
	{                                                                      \
		.children = children_,                                         \
		.hw_data = (void *)hw_data_,                                   \
		.api = api_,                                                   \
	}

/**
 * @brief Section name for clock object
 *
 * Section name for clock object. Each clock object uses a named section so
 * the linker can optimize unused clocks out of the build.
 */
#define Z_CLOCK_SECTION_NAME(clk_id) _CONCAT(.clk_node, clk_id)

/**
 * @brief Define a @ref clk object
 *
 * Defines and initializes configuration and data fields of a @ref clk
 * object
 * @param node_id The devicetree node identifier.
 * @param clk_id clock identifier (used to name the defined @ref clk).
 * @param hw_data Pointer to the clock's private data, which will be
 * stored in the @ref clk.hw_data field. This data may be in ROM or RAM.
 * @param config Pointer to the clock's private constant data, which will be
 * stored in the @ref clk.config field
 * @param api Pointer to the clock's API structure.
 */
#define Z_CLOCK_BASE_DEFINE(node_id, clk_id, hw_data, api)                     \
	Z_CLOCK_DEFINE_CHILDREN(node_id);                                      \
	const struct clk Z_GENERIC_SECTION(Z_CLOCK_SECTION_NAME(clk_id))       \
		CLOCK_NAME_GET(clk_id) =                                       \
		Z_CLOCK_INIT(Z_CLOCK_GET_CHILDREN(node_id),                    \
			     hw_data, api);

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

DT_FOREACH_STATUS_OKAY_NODE(Z_MAYBE_CLOCK_DECLARE_INTERNAL)

/**
 * @brief Helper to get a clock dependency ordinal if the clock is referenced
 *
 * The build system will convert these dependency ordinals into clock object
 * references after the first link phase is completed
 * @param node_id Clock identifier
 */
#define Z_GET_CLOCK_DEP_ORD(node_id)                                           \
	IF_ENABLED(DT_NODE_HAS_STATUS(node_id, okay),                          \
		(DT_DEP_ORD(node_id),))

/**
 * @brief Clock dependency array name
 * @param node_id Clock identifier
 */
#define Z_CLOCK_CHILDREN_NAME(node_id)                                         \
	_CONCAT(__clock_children_, Z_CLOCK_DT_CLK_ID(node_id))

/**
 * @brief Define clock children array
 *
 * This macro defines a clock children array. A reference to
 * the clock dependency array can be retrieved with `Z_CLOCK_GET_CHILDREN`
 *
 * In the initial build, this array will expand to a list of clock ordinal
 * numbers that describe children of the clock, like so:
 * @code{.c}
 *     const clock_handle_t __weak __clock_children_clk_dts_ord_45[] = {
 *         66,
 *         30,
 *         55,
 *     }
 * @endcode
 *
 * In the second pass of the build, gen_clock_deps.py will create a strong
 * symbol to override the weak one, with each ordinal number resolved to
 * a clock handle (or omitted, if no clock structure was defined in the
 * build). The final array will look like so:
 * @code{.c}
 *     const clock_handle_t __clock_children_clk_dts_ord_45[] = {
 *         30, // Handle for clock with ordinal 66
 *         // Clock structure for ordinal 30 was not linked in build
 *         16, // Handle for clock with ordinal 55
 *         CLOCK_LIST_END, // Sentinel for end of list
 *     }
 * @endcode
 * This multi-phase build is necessary so that the linker will optimize out
 * any clock object that are not referenced elsewhere in the build. This way,
 * a clock object will be discarded in the first link phase unless another
 * structure references it (such as a clock referencing its parent object)
 * @param node_id Clock identifier
 */
#define Z_CLOCK_DEFINE_CHILDREN(node_id)                                       \
	const clock_handle_t __weak Z_CLOCK_CHILDREN_NAME(node_id)[] =         \
		{DT_SUPPORTS_CLK_ORDS(node_id)};

/**
 * @brief Get clock dependency array
 *
 * This macro gets the c identifier for the clock dependency array,
 * declared with `CLOCK_DEFINE_DEPS`, which will contain
 * an array of pointers to the clock objects dependent on this clock.
 * @param node_id Clock identifier
 */
#define Z_CLOCK_GET_CHILDREN(node_id) Z_CLOCK_CHILDREN_NAME(node_id)


/** @endcond */

/**
 * @brief Get the clock corresponding to a handle
 *
 * @param clock_handle the clock handle
 *
 * @return the clock that has thaT handle, or a null pointer if @p clock_handle
 * does not identify a clock.
 */
static inline const struct clk *clk_from_handle(clock_handle_t clock_handle)
{
	STRUCT_SECTION_START_EXTERN(clk);
	const struct clk *clk = NULL;
	size_t numclk;

	STRUCT_SECTION_COUNT(clk, &numclk);

	if ((clock_handle > 0) && ((size_t)clock_handle <= numclk)) {
		clk = &STRUCT_SECTION_START(clk)[clock_handle - 1];
	}

	return clk;
}

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
 * @param hw_data Pointer to the clock's private data, which will be
 * stored in the @ref clk.hw_data field. This data may be in ROM or RAM.
 * @param api Pointer to the clock's API structure.
 */

#define CLOCK_DT_DEFINE(node_id, hw_data, api, ...)                            \
	Z_CLOCK_BASE_DEFINE(node_id, Z_CLOCK_DT_CLK_ID(node_id), hw_data,      \
			    api)

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

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_CLOCK_H_ */
