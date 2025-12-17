/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT nxp_spc_pd

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>

#include <zephyr/logging/log.h>

#include <fsl_spc.h>
LOG_MODULE_REGISTER(nxp_spc_pd, CONFIG_POWER_DOMAIN_LOG_LEVEL);

struct pd_spc_config {
	SPC_Type *spc_base;
	uint8_t pd_ctrl_bit;
};

struct pd_spc_data {
	uint32_t on_count;
};

static int pd_spc_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct pd_spc_config *config = dev->config;
	struct pd_spc_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON: {
		if (config->pd_ctrl_bit != 0xFF) {
			config->spc_base->EVD_CFG &=
				~(SPC_EVD_CFG_REG_EVDISO(1U << config->pd_ctrl_bit) |
				  SPC_EVD_CFG_REG_EVDLPISO(1U << config->pd_ctrl_bit));
		}
		data->on_count++;
		break;
	}
	case PM_DEVICE_ACTION_TURN_OFF: {
		data->on_count--;
		if (config->pd_ctrl_bit != 0xFF) {
			if (data->on_count == 0) {
				config->spc_base->EVD_CFG |=
					SPC_EVD_CFG_REG_EVDLPISO(1U << config->pd_ctrl_bit);
			}
		}
		break;
	}
	case PM_DEVICE_ACTION_SUSPEND:
		break;

	case PM_DEVICE_ACTION_RESUME:
		break;
	default:
		break;
	}

	return 0;
}

static int pd_spc_init(const struct device *dev)
{
	struct pd_spc_data *data = dev->data;

	data->on_count = 0;

	return pm_device_driver_init(dev, pd_spc_pm_action);
}

#define POWER_DOMAIN_DEVICE(inst)                                                                  \
	static const struct pd_spc_config pd_spc_config_##inst = {                                 \
		.spc_base = (SPC_Type *)DT_INST_REG_ADDR(inst),                                    \
		.pd_ctrl_bit = DT_INST_PROP_OR(inst, pd_ctrl_bit, 0xFF),                           \
	};                                                                                         \
	static struct pd_spc_data pd_spc_data_##inst = {                                           \
		.on_count = 0,                                                                     \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, pd_spc_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, pd_spc_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &pd_spc_data_##inst, &pd_spc_config_##inst,                         \
			      PRE_KERNEL_1, CONFIG_POWER_DOMAIN_SPC_INIT_PRIORITY,                  \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(POWER_DOMAIN_DEVICE)
