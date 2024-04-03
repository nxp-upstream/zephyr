/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#include "../clock_mgmt_common.h"

#define DT_DRV_COMPAT nxp_syscon_clock_gate

struct syscon_clock_gate_config {
	const struct clk *const *children;
	uint8_t child_count;
	const struct clk *parent;
	volatile uint32_t *reg;
	uint8_t enable_offset;
};

int syscon_clock_gate_get_rate(const struct clk *clk)
{
	const struct syscon_clock_gate_config *config = clk->config;

	return ((*config->reg) & BIT(config->enable_offset)) ?
		clock_get_rate(config->parent) : 0;
}

int syscon_clock_gate_configure(const struct clk *clk, void *data)
{
	const struct syscon_clock_gate_config *config = clk->config;
	bool ungate = (bool)data;

	if (ungate) {
		(*config->reg) |= BIT(config->enable_offset);
	} else {
		(*config->reg) &= ~BIT(config->enable_offset);
	}
	return 0;
}

const struct clock_driver_api nxp_syscon_gate_api = {
	.get_rate = syscon_clock_gate_get_rate,
	.configure = syscon_clock_gate_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct clk *const nxp_syscon_gate_children_##inst[] =            \
		CLOCK_INST_GET_DEPS(inst);                                     \
	                                                                       \
	const struct syscon_clock_gate_config nxp_syscon_gate_##inst = {       \
		.children = nxp_syscon_gate_children_##inst,                   \
		.child_count = ARRAY_SIZE(nxp_syscon_gate_children_##inst),    \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.enable_offset = (uint8_t)DT_INST_PROP(inst, offset),          \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     NULL,                                             \
			     &nxp_syscon_gate_##inst,                          \
			     &nxp_syscon_gate_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
