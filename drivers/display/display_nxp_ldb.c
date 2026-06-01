/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_ldb

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/ldb.h>
#include <errno.h>

LOG_MODULE_REGISTER(ldb, CONFIG_DISPLAY_LOG_LEVEL);

#include "fsl_ldb.h"

struct ldb_mcux_config {
	uintptr_t base;
	struct ldb_link_cfg boot_cfg;
	bool enable_on_boot;
};

struct ldb_mcux_data {
	struct ldb_link_cfg cfg;
	bool configured;
	bool enabled;
};

static int ldb_configure_dev(const struct device *dev, const struct ldb_link_cfg *cfg)
{
	struct ldb_mcux_data *data = dev->data;

	if (data->enabled) {
		return -EBUSY;
	}

	data->cfg = *cfg;
	data->configured = true;
	return 0;
}

static int ldb_enable_dev(const struct device *dev)
{
	const struct ldb_mcux_config *cfg = dev->config;
	struct ldb_mcux_data *data = dev->data;

	if (data->enabled) {
		return 0;
	}

	/* If not configured by caller, fall back to DT boot config */
	if (!data->configured) {
		data->cfg = cfg->boot_cfg;
		data->configured = true;
	}

	if (data->cfg.input > 1) {
		return -EINVAL;
	}

	const uint8_t hal_mapping = (uint8_t)data->cfg.bit_mapping;
	const uint8_t hal_dual_panel =
		(data->cfg.mode_flags & LDB_MODE_DUAL_PANEL) ? 1U : 0U;

	LDB_Init((LDB_Type *)cfg->base, data->cfg.input, hal_dual_panel, hal_mapping);

	data->enabled = true;
	return 0;
}

static int ldb_disable_dev(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* If your HAL provides LDB_Disable/Deinit, call it here. Otherwise: */
	return -ENOSYS;
}

static int ldb_init(const struct device *dev)
{
	const struct ldb_mcux_config *cfg = dev->config;

	if (cfg->enable_on_boot) {
		return ldb_enable_dev(dev);
	}

	return 0;
}

static DEVICE_API(ldb, ldb_api) = {
	.configure = ldb_configure_dev,
	.enable = ldb_enable_dev,
	.disable = ldb_disable_dev,
};

#define LDB_INIT(inst)                                                               \
	static struct ldb_mcux_data ldb_data_##inst;                                 \
	static const struct ldb_mcux_config ldb_cfg_##inst = {                       \
		.base = DT_INST_REG_ADDR(inst),                                       \
		.boot_cfg = {                                                        \
			.input = DT_INST_PROP(inst, display_index),                   \
			.bit_mapping = DT_INST_ENUM_IDX(inst, bit_mapping),           \
			.mode_flags = DT_INST_PROP_OR(inst, dual_panel, 0) ?          \
				LDB_MODE_DUAL_PANEL : 0,                              \
		},                                                                    \
		.enable_on_boot = DT_INST_PROP_OR(inst, enable_on_boot, 0),           \
	};                                                                            \
	DEVICE_DT_INST_DEFINE(inst, ldb_init, NULL,                                   \
			      &ldb_data_##inst, &ldb_cfg_##inst,                    \
			      POST_KERNEL, CONFIG_DISPLAY_LDB_INIT_PRIORITY,        \
			      &ldb_api);

DT_INST_FOREACH_STATUS_OKAY(LDB_INIT)
