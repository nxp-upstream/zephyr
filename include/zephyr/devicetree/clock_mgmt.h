/**
 * @file
 * @brief Clock Management Devicetree macro public API header file.
 */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_
#define ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/devicetree.h>

/**
 * @defgroup devicetree-clock-mgmt Devicetree Clock Management API
 * @ingroup devicetree
 * @{
 */

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

/**
 * @brief Is the clock node referenced by @p node_id referenced?
 *
 * A clock is considered "referenced" if an node with status `okay` references
 * the clock node's phandle within its `clock-outputs` or `clock-state-<n>`
 * clock properties. If a clock node is referenced, all the nodes which it
 * references or is a child of will also be considered referenced. This applies
 * recursively.
 * @param node_id Clock identifier
 * @return 1 if clock is referenced
 */
#define DT_CLOCK_USED(node_id) DT_CAT(node_id, _CLOCK_USED)

/**
 * @brief Number of clock management states for a node identifier
 * Gets the number of clock management states for a given node identifier
 * @param node_id Node identifier to get the number of states for
 */
#define DT_NUM_CLOCK_MGMT_STATES(node_id) \
	DT_CAT(node_id, _CLOCK_STATE_NUM)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif


#endif  /* ZEPHYR_INCLUDE_DEVICETREE_CLOCK_MGMT_H_ */
