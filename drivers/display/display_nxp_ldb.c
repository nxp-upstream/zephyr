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

/*
 * HAL integration
 * Replace include + prototype/constants with your real MCUX/NXP driver API.
 */
#include "fsl_ldb.h"

#ifndef LVDS_JEIDA
#define LVDS_JEIDA 1
#endif
#ifndef LVDS_SPWG
#define LVDS_SPWG  0
#endif
#ifndef LDB_DUAL_PANEL
#define LDB_DUAL_PANEL 1
#endif
#ifndef LDB_SINGLE_PANEL
#define LDB_SINGLE_PANEL 0
#endif

/* Example prototype (ADJUST TO REAL ONE) */
extern void LDB_Init(void *base, uint8_t display_index, int dual_panel, int mapping);

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

	const int hal_mapping =
		(data->cfg.bit_mapping == LDB_BIT_MAPPING_JEIDA) ? LVDS_JEIDA : LVDS_SPWG;

	const int hal_dual_panel =
		(data->cfg.mode_flags & LDB_MODE_DUAL_PANEL) ? LDB_DUAL_PANEL : LDB_SINGLE_PANEL;

	LDB_Init((void *)cfg->base, data->cfg.input, hal_dual_panel, hal_mapping);

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

/* DT decode: string enum -> C enum */
#define LDB_BIT_MAPPING_FROM_DT(inst) \
	LDB_BIT_MAPPING_##DT_INST_STRING_UPPER_TOKEN(inst, bit_mapping)

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
			.bit_mapping = LDB_BIT_MAPPING_FROM_DT(inst),                 \
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
