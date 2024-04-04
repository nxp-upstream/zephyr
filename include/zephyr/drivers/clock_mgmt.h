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
#include <clock_mgmt_soc.h>

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

/**
 * @brief Clock management callback data
 *
 * Describes clock management callback data. Drivers should not directly access
 * or modify these fields.
 */
struct clock_mgmt_callback {
	void (*clock_callback)(void *user_data);
	void *user_data;
};

/**
 * @brief Clock management state data
 *
 * Describes clock management state data. Drivers should not directly access
 * or modify these fields.
 */
struct clock_mgmt_state {
	/** Clocks to configure for this state */
	const struct clk *const *clocks;
	/** Clock data tokens to pass to each clock */
	const void **clock_config_data;
	/** Number of clocks in this state */
	const uint8_t num_clocks;
};

/**
 * @brief Clock management configuration
 *
 * Describes clock management data for a device. Drivers should not access or
 * modify these fields.
 */
struct clock_mgmt {
	/** Clock outputs to read rates from */
	const struct clk *const *outputs;
	/** States to configure */
	const struct clock_mgmt_state *const *states;
	/** Count of clock outputs */
	const uint8_t output_count;
	/** Count of clock states */
	const uint8_t state_count;
};

/** @cond INTERNAL_HIDDEN */

/**
 * @brief Defines clock management data for a specific clock
 *
 * Defines clock management data for a clock, based on the clock's compatible
 * string. Given clock nodes with compatibles like so:
 *
 * @code{.dts}
 *     a {
 *             compatible = "vnd,source";
 *     };
 *
 *     b {
 *             compatible = "vnd,mux";
 *     };
 *
 *     c {
 *             compatible = "vnd,div";
 *     };
 * @endcode
 *
 * The clock driver must provide definitions like so:
 *
 * @code{.c}
 *     #define Z_CLOCK_MGMT_VND_SOURCE_DATA_DEFINE(node_id, prop, idx)
 *     #define Z_CLOCK_MGMT_VND_MUX_DATA_DEFINE(node_id, prop, idx)
 *     #define Z_CLOCK_MGMT_VND_DIV_DATA_DEFINE(node_id, prop, idx)
 * @endcode
 *
 * All macros take the node id of the node with the clock-state-i, the name of
 * the clock-state-i property, and the index of the phandle for this clock node
 * as arguments. The _DATA_DEFINE macros should initialize any data structure
 * needed by the clock.
 *
 * @param node_id Node identifier
 * @param prop clock property name
 * @param idx property index
 */
#define Z_CLOCK_MGMT_CLK_DATA_DEFINE(node_id, prop, idx)                       \
	_CONCAT(_CONCAT(Z_CLOCK_MGMT_, DT_STRING_UPPER_TOKEN(                  \
		DT_PHANDLE_BY_IDX(node_id, prop, idx), compatible_IDX_0)),     \
		_DATA_DEFINE)(node_id, prop, idx);

/**
 * @brief Gets clock management data for a specific clock
 *
 * Reads clock management data for a clock, based on the clock's compatible
 * string. Given clock nodes with compatibles like so:
 *
 * @code{.dts}
 *     a {
 *             compatible = "vnd,source";
 *     };
 *
 *     b {
 *             compatible = "vnd,mux";
 *     };
 *
 *     c {
 *             compatible = "vnd,div";
 *     };
 * @endcode
 *
 * The clock driver must provide definitions like so:
 *
 * @code{.c}
 *     #define Z_CLOCK_MGMT_VND_SOURCE_DATA_GET(node_id, prop, idx)
 *     #define Z_CLOCK_MGMT_VND_MUX_DATA_GET(node_id, prop, idx)
 *     #define Z_CLOCK_MGMT_VND_DIV_DATA_GET(node_id, prop, idx)
 * @endcode
 *
 * All macros take the node id of the node with the clock-state-i, the name of
 * the clock-state-i property, and the index of the phandle for this clock node
 * as arguments.
 * The _DATA_GET macros should get a reference to the clock data structure
 * data structure, which will be cast to a void pointer by the clock management
 * subsystem.
 * @param node_id Node identifier
 * @param prop clock property name
 * @param idx property index
 */
#define Z_CLOCK_MGMT_CLK_DATA_GET(node_id, prop, idx)                          \
	(void *)_CONCAT(_CONCAT(Z_CLOCK_MGMT_, DT_STRING_UPPER_TOKEN(          \
		DT_PHANDLE_BY_IDX(node_id, prop, idx), compatible_IDX_0)),     \
		_DATA_GET)(node_id, prop, idx)

/**
 * @brief Gets pointer to @ref clk structure for a given node property index
 * @param node_id Node identifier
 * @param prop clock property name
 * @param idx property index
 */
#define Z_CLOCK_MGMT_GET_REF(node_id, prop, idx)                               \
	CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx))

/**
 * @brief Clock outputs array name
 * @param node_id Node identifier
 */
#define Z_CLOCK_MGMT_OUTPUTS_NAME(node_id)                                     \
	_CONCAT(Z_CLOCK_DT_CLK_ID(node_id), _clock_outputs)

/**
 * @brief Individual clock state name
 * @param node_id Node identifier
 * @param idx clock state index
 */
#define Z_CLOCK_MGMT_STATE_NAME(node_id, idx)                                  \
	_CONCAT(_CONCAT(Z_CLOCK_DT_CLK_ID(node_id), _clock_state_), idx)

/**
 * @brief Clock states array name
 * @param node_id Node identifier
 */
#define Z_CLOCK_MGMT_STATES_NAME(node_id)                                      \
	_CONCAT(Z_CLOCK_DT_CLK_ID(node_id), _clock_states)
/**
 * @brief Defines a clock management state for a given index
 * @param idx State index to define
 * @param node_id node identifier state is defined on
 */
#define Z_CLOCK_MGMT_STATE_DEFINE(idx, node_id)                                \
	const struct clk *const                                                \
		_CONCAT(Z_CLOCK_MGMT_STATE_NAME(node_id, idx), _clocks)[] = {  \
			DT_FOREACH_PROP_ELEM_SEP(node_id,                      \
			_CONCAT(clock_state_, idx), Z_CLOCK_MGMT_GET_REF, (,)) \
	};                                                                     \
	DT_FOREACH_PROP_ELEM_SEP(node_id,                                      \
	_CONCAT(clock_state_, idx),                                            \
	Z_CLOCK_MGMT_CLK_DATA_DEFINE, (;))                                     \
	const void *CONCAT(Z_CLOCK_MGMT_STATE_NAME(node_id, idx),              \
		_clock_data)[] = {                                             \
			DT_FOREACH_PROP_ELEM_SEP(node_id,                      \
			_CONCAT(clock_state_, idx),                            \
			Z_CLOCK_MGMT_CLK_DATA_GET, (,))                        \
	};                                                                     \
	const struct clock_mgmt_state                                          \
		Z_CLOCK_MGMT_STATE_NAME(node_id, idx) = {                      \
		.clocks = _CONCAT(Z_CLOCK_MGMT_STATE_NAME(node_id, idx), _clocks), \
		.num_clocks = DT_PROP_LEN(node_id, _CONCAT(clock_state_, idx)), \
	};

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
 * @param clk_id Clock identifier for the node
 */
#define Z_CLOCK_DEFINE_OUTPUT(node_id, clk_id)                                 \
	/* Clock management API, with notification callback */                 \
	const struct clock_mgmt_clk_api _CONCAT(clk_id, _clk_api);             \
	/* Clock management callback structure, stored in RAM */               \
	struct clock_mgmt_callback _CONCAT(clk_id, _callback);                 \
	CLOCK_DT_DEFINE(node_id, &_CONCAT(clk_id, _callback),                  \
		NULL, ((struct clock_driver_api *)&_CONCAT(clk_id, _clk_api)));


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
	const struct clk *Z_CLOCK_MGMT_OUTPUTS_NAME(node_id) =                 \
		{DT_FOREACH_PROP_ELEM_SEP(node_id, clock_outputs,              \
					Z_CLOCK_MGMT_GET_REF, (,))};           \
	/* Define clock states */                                              \
	LISTIFY(DT_NUM_CLOCK_MGMT_STATES(node_id),                             \
		Z_CLOCK_MGMT_STATE_DEFINE, (;), node_id);                      \
	/* Define clock API and structure */                                   \
	IF_ENABLED(DT_CLOCK_USED(node_id), (Z_CLOCK_DEFINE_OUTPUT(node_id,     \
		Z_CLOCK_DT_CLK_ID(node_id))))



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
