/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_clock_source

struct syscon_clock_source_config {
	uint32_t rate;
	volatile uint32_t *reg;
	uint8_t enable_offset;
};

int syscon_clock_source_get_rate(const struct clk *clk)
{
	const struct syscon_clock_source_config *config = clk->config;

	if (config->reg == NULL) {
		return config->rate;
	}

	return ((*config->reg) & BIT(config->enable_offset)) ?
		config->rate : 0;
}

int syscon_clock_source_configure(const struct clk *clk, void *data)
{
	const struct syscon_clock_source_config *config = clk->config;
	bool ungate = (bool)data;

	if (config->reg == NULL) {
		return 0;
	}

	if (ungate) {
		(*config->reg) |= BIT(config->enable_offset);
	} else {
		(*config->reg) &= ~BIT(config->enable_offset);
	}
	return 0;
}

const struct clock_driver_api nxp_syscon_source_api = {
	.get_rate = syscon_clock_source_get_rate,
	.configure = syscon_clock_source_configure,
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct syscon_clock_source_config nxp_syscon_source_##inst = {   \
		.rate = DT_INST_PROP(inst, frequency),                         \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.enable_offset = (uint8_t)DT_INST_PROP(inst, offset),          \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, NULL, NULL,                                 \
			     &nxp_syscon_source_##inst,                        \
			     &nxp_syscon_source_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
