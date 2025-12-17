/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_cmc_pd

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nxp_cmc_pd, CONFIG_POWER_DOMAIN_LOG_LEVEL);

/**
 * @brief Synthesize the name of the object that holds on-off disabling power states.
 *
 * @param dev_id Device identifier.
 */
#define PM_ON_OFF_CONSTRAINTS_NAME(node_id) _CONCAT(__pdonoffconstraints_, node_id)

/**
 * @brief Synthesize the name of the object that holds resume-suspend disabling power states.
 *
 * @param dev_id Device identifier.
 */
#define PM_RESUME_SUSPEND_CONSTRAINTS_NAME(node_id) _CONCAT(__pdresumeconstraints_, node_id)

/**
 * @brief Helper macro to define a single on-off pm constraint.
 */
#define PM_ON_OFF_CONSTRAINT_DEFINE(i, node_id)                                                    \
	COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_PHANDLE_BY_IDX(node_id,          \
		on_off_disabling_power_states, i)),                              \
		(PM_STATE_CONSTRAINT_INIT(DT_PHANDLE_BY_IDX(node_id,            \
		on_off_disabling_power_states, i)),), ())

/**
 * @brief Helper macro to define a single resume-suspend pm constraint.
 */
#define PM_RESUME_SUSPEND_CONSTRAINT_DEFINE(i, node_id)                                            \
	COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_PHANDLE_BY_IDX(node_id,          \
		resume_suspend_disabling_power_states, i)),                      \
		(PM_STATE_CONSTRAINT_INIT(DT_PHANDLE_BY_IDX(node_id,            \
		resume_suspend_disabling_power_states, i)),), ())

/**
 * @brief Helper macro to generate a list of on-off pm constraints.
 */
#define PM_ON_OFF_CONSTRAINTS_DEFINE(node_id)                                                      \
	{                                                                                          \
		LISTIFY(DT_PROP_LEN_OR(node_id, on_off_disabling_power_states, 0),     \
			PM_ON_OFF_CONSTRAINT_DEFINE, (), node_id)           \
	}

/**
 * @brief Helper macro to generate a list of resume-suspend pm constraints.
 */
#define PM_RESUME_SUSPEND_CONSTRAINTS_DEFINE(node_id)                                              \
	{                                                                                          \
		LISTIFY(DT_PROP_LEN_OR(node_id, resume_suspend_disabling_power_states, 0), \
			PM_RESUME_SUSPEND_CONSTRAINT_DEFINE, (), node_id)       \
	}

/**
 * @brief Helper macro to define an array of on-off pm constraints.
 */
#define ON_OFF_CONSTRAINTS_DEFINE(node_id)                                                         \
	Z_DECL_ALIGN(struct pm_state_constraint)                                                   \
	PM_ON_OFF_CONSTRAINTS_NAME(node_id)[] = PM_ON_OFF_CONSTRAINTS_DEFINE(node_id);

/**
 * @brief Helper macro to define an array of resume-suspend pm constraints.
 */
#define RESUME_SUSPEND_CONSTRAINTS_DEFINE(node_id)                                                 \
	Z_DECL_ALIGN(struct pm_state_constraint)                                                   \
	PM_RESUME_SUSPEND_CONSTRAINTS_NAME(node_id)                                                \
	[] = PM_RESUME_SUSPEND_CONSTRAINTS_DEFINE(node_id);

/**
 * @brief Helper macro to conditionally define on-off constraints array.
 */
/* Use standard PM_STATE_CONSTRAINTS_LIST_DEFINE/GET macros for lists and
 * initialization. These create the constraint list and a pm_state_constraints
 * initializer suitable for assigning to struct pm_state_constraints fields.
 */

struct pd_cmc_config {
	struct pm_state_constraints on_off_constraints;
	struct pm_state_constraints resume_suspend_constraints;
};

static int pd_cmc_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct pd_cmc_config *config = (struct pd_cmc_config *)dev->config;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON: {
		/* Once turn on, lock power states which will power off current power domain. */
		pm_policy_state_constraints_get(&(config->on_off_constraints));
		break;
	}
	case PM_DEVICE_ACTION_TURN_OFF: {
		/* If turn off current power down, unlock power state which power off current power
		 * domain. */
		pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_OFF, NULL);
		pm_policy_state_constraints_put(&(config->on_off_constraints));
		break;
	}
	case PM_DEVICE_ACTION_SUSPEND: {
		pm_device_children_action_run(dev, PM_DEVICE_ACTION_SUSPEND, NULL);
		pm_policy_state_constraints_put(&(config->resume_suspend_constraints));
		break;
	}
	case PM_DEVICE_ACTION_RESUME: {
		pm_policy_state_constraints_get(&(config->resume_suspend_constraints));
		break;
	}
	default:
		break;
	}

	return 0;
}

static int pd_cmc_init(const struct device *dev)
{
	return pm_device_driver_init(dev, pd_cmc_pm_action);
}

#define DEFINE_ON_OFF_CONSTRAINTS(inst)                                                            \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, on_off_disabling_power_states),                   \
		    (PM_STATE_CONSTRAINTS_LIST_DEFINE(DT_DRV_INST(inst),                           \
						      on_off_disabling_power_states);),            \
		    ())

#define DEFINE_RESUME_SUSPEND_CONSTRAINTS(inst)                                                    \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, resume_suspend_disabling_power_states),           \
		    (PM_STATE_CONSTRAINTS_LIST_DEFINE(DT_DRV_INST(inst),                           \
						      resume_suspend_disabling_power_states);),    \
		    ())

#define GET_ON_OFF_CONSTRAINTS(inst)                                                               \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, on_off_disabling_power_states),                   \
		    (PM_STATE_CONSTRAINTS_GET(DT_DRV_INST(inst),                                   \
					      on_off_disabling_power_states)),                     \
		    ({.list = NULL, .count = 0}))

#define GET_RESUME_SUSPEND_CONSTRAINTS(inst)                                                       \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, resume_suspend_disabling_power_states),           \
		    (PM_STATE_CONSTRAINTS_GET(DT_DRV_INST(inst),                                   \
					      resume_suspend_disabling_power_states)),             \
		    ({.list = NULL, .count = 0}))

#define POWER_DOMAIN_DEVICE(inst)                                                                  \
	DEFINE_ON_OFF_CONSTRAINTS(inst)                                                            \
	DEFINE_RESUME_SUSPEND_CONSTRAINTS(inst)                                                    \
	static const struct pd_cmc_config pd_cmc_config_##inst = {                                 \
		.on_off_constraints = GET_ON_OFF_CONSTRAINTS(inst),                                \
		.resume_suspend_constraints = GET_RESUME_SUSPEND_CONSTRAINTS(inst),               \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, pd_cmc_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, pd_cmc_init, PM_DEVICE_DT_INST_GET(inst), NULL,               \
			      &pd_cmc_config_##inst, PRE_KERNEL_1, CONFIG_POWER_DOMAIN_CMC_INIT_PRIORITY, \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(POWER_DOMAIN_DEVICE)
