/*
 * Copyright 2024 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * Public APIs for clock management
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_H_

/**
 * @brief Clock Management Interface
 * @defgroup clock_mgmt_interface Clock management Interface
 * @ingroup io_interfaces
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/clock_mgmt/clock_driver.h>
#include <errno.h>

/**
 * @name Clock Management States
 * @{
 */

/** Default state (state used when the device is in operational state). */
#define CLOCK_MGMT_STATE_DEFAULT 0U
/** Sleep state (state used when the device is in low power mode). */
#define CLOCK_MGMT_STATE_SLEEP 1U

/** This and higher values refer to custom private states. */
#define CLOCK_MGMT_STATE_PRIV_START 2U

/** @} */

/**
 * @name Clock Subsys Names
 * @{
 */

/** Default clock subsystem */
#define CLOCK_MGMT_SUBSYS_DEFAULT 0U
/** This and higher values refer to custom private clock subsystems */
#define CLOCK_MGMT_SUBSYS_PRIV_START 1U

/** @} */

/** Clock management configuration */
struct clock_mgmt {
	/** Clock outputs to read rates from */
	const struct clk *const *outputs;
	/** States to configure */
	const struct clock_mgmt_state *const *states;
	/** Callback tracking data */
	const struct clock_mgmt_callback *callback;
	/** Count of clock outputs */
	const uint8_t output_count;
	/** Count of clock states */
	const uint8_t state_count;
};

/** @cond INTERNAL_HIDDEN */

/**
 * @brief Gets pointer to @ref clk structure for a given node property index
 * @param node_id Node identifier
 * @param prop output clocks property name
 * @param idx property index
 */
#define Z_CLOCK_MGMT_GET_REF(node_id, prop, idx)                               \
	CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx))

/**
 * @brief Clock outputs array name
 * @param node_id Node identifier
 */
#define Z_CLOCK_MGMT_OUTPUTS_NAME(node_id) _CONCAT(node_id, _clock_outputs)

/**
 * @brief Clock states array name
 * @param node_id Node identifier
 */
#define Z_CLOCK_MGMT_STATES_NAME(node_id) _CONCAT(node_id, _clock_states)

/**
 * @brief Defines a clock management state for a given index
 * @param idx State index to define
 * @param node_id node identifier state is defined on
 */
#define Z_CLOCK_MGMT_STATE_DEFINE(idx, node_id) {NULL}

/**
 * @brief Internal API definition for clock consumers
 *
 * Since clock consumers only need to implement the "notify" API, we use
 * a reduced API pointer. Since the notify field is the first entry in
 * both structures, we can still receive callbacks via this API structure.
 */
struct clock_mgmt_clk_api {
	/** Notify clock consumer of rate change */
	int (*notify)(const struct clk *clk, const struct clk *parent);
};

/**
 * @brief Define a @ref clk structure for clock consumer
 *
 * This structure is used to receive "notify" callbacks from the parent
 * clock devices, and will pass the notifications onto the clock consumer
 * @param node_id Node identifier to define clock structure for
 */
#define Z_CLOCK_DEFINE_OUTPUT(node_id)                                         \
	static const struct clock_mgmt_clk_api _CONCAT(node_id, _clk_api);     \
	CLOCK_DT_DEFINE(node_id, NULL, NULL,                                   \
		((const struct clock_driver_api *)&_CONCAT(node_id, _clk_api)));


/** @endcond */

/**
 * @brief Defines clock management information for a given node identifier.
 *
 * This macro should be called during the device definition. It will define
 * and initialize the clock management configuration for the device represented
 * by node_id. Clock subsystems as well as clock management states will be
 * initialized by this macro.
 * @param node_id node identifier to define clock management data for
 */
#define CLOCK_MGMT_DEFINE(node_id)					       \
	/* Define clock outputs */                                             \
	const struct clk *const *Z_CLOCK_MGMT_OUTPUTS_NAME(node_id) =          \
		{DT_FOREACH_PROP_ELEM_SEP(node_id, clock_outputs,              \
					Z_CLOCK_MGMT_GET_REF, (,))};           \
	/* Define clock states */                                              \
	const struct clock_mgmt_state *const                                   \
		*Z_CLOCK_MGMT_STATES_NAME(node_id) =                           \
		{LISTIFY(DT_NUM_CLOCK_MGMT_STATES(node_id),                    \
		Z_CLOCK_MGMT_STATE_DEFINE, (,), node_id)};                     \
	/* Define clock API and structure */                                   \
	IF_ENABLED(DT_CLOCK_USED(node_id), (Z_CLOCK_DEFINE_OUTPUT(node_id)))



/**
 * @brief Defines clock management information for a given driver instance
 * Equivalent to CLOCK_MGMT_DEFINE(DT_DRV_INST(inst))
 * @param inst Driver instance number
 */
#define CLOCK_MGMT_DT_INST_DEFINE(inst) CLOCK_MGMT_DEFINE(DT_DRV_INST(inst))


#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_MGMT_H_ */
