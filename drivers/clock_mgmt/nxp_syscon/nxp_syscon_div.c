/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#include "../clock_mgmt_common.h"

#define DT_DRV_COMPAT nxp_syscon_clock_div

struct syscon_clock_div_config {
	const struct clk *const *children;
	uint8_t child_count;
	uint8_t mask_width;
	const struct clk *parent;
	volatile uint32_t *reg;
};

int syscon_clock_div_get_rate(const struct clk *clk)
{
	const struct syscon_clock_div_config *config = clk->config;
	int parent_rate = clock_get_rate(config->parent);
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));

	if (parent_rate <= 0) {
		return parent_rate;
	}

	/* Calculate divided clock */
	return parent_rate / ((*config->reg & div_mask) + 1);
}

int syscon_clock_div_configure(const struct clk *clk, void *div)
{
	const struct syscon_clock_div_config *config = clk->config;
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));
	uint32_t div_val = (((uint32_t)div) - 1) & div_mask;


	(*config->reg) = ((*config->reg) & ~div_mask) | div_val;
	return 0;
}

const struct clock_driver_api nxp_syscon_div_api = {
	.get_rate = syscon_clock_div_get_rate,
	.configure = syscon_clock_div_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	CLOCK_INST_DEFINE_DEPS(inst);                                          \
                                                                               \
	const struct syscon_clock_div_config nxp_syscon_div_##inst = {         \
		.children = CLOCK_INST_GET_DEPS(inst),                         \
		.child_count = CLOCK_INST_NUM_DEPS(inst),                      \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = (uint8_t)DT_INST_REG_SIZE(inst),                 \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, NULL,                                       \
			     &nxp_syscon_div_##inst,                           \
			     &nxp_syscon_div_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
