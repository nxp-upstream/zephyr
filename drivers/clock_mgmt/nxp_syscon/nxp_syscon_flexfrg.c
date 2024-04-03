/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#include "../clock_mgmt_common.h"

#define DT_DRV_COMPAT nxp_syscon_flexfrg

struct syscon_clock_frg_config {
	const struct clk *const *children;
	uint8_t child_count;
	const struct clk *parent;
	volatile uint32_t *reg;
};

#define SYSCON_FLEXFRGXCTRL_DIV_MASK 0xFF
#define SYSCON_FLEXFRGXCTRL_MULT_MASK 0xFF00

int syscon_clock_frg_get_rate(const struct clk *clk)
{
	const struct syscon_clock_frg_config *config = clk->config;
	int parent_rate = clock_get_rate(config->parent);
	int frg_factor;

	if (parent_rate <= 0) {
		return parent_rate;
	}

	/* Calculate rate */
	frg_factor = FIELD_GET(SYSCON_FLEXFRGXCTRL_MULT_MASK, (*config->reg));

	/* DIV value must be 256, no need to read it */
	frg_factor /= SYSCON_FLEXFRGXCTRL_DIV_MASK;
	parent_rate /= (1 + frg_factor);
	return parent_rate;
}

int syscon_clock_frg_configure(const struct clk *clk, void *mult)
{
	const struct syscon_clock_frg_config *config = clk->config;
	uint32_t mult_val = FIELD_PREP(SYSCON_FLEXFRGXCTRL_MULT_MASK, ((uint32_t)mult));


	/* DIV field should always be 0xFF */
	(*config->reg) = mult_val | SYSCON_FLEXFRGXCTRL_DIV_MASK;
	return 0;
}

const struct clock_driver_api nxp_syscon_frg_api = {
	.get_rate = syscon_clock_frg_get_rate,
	.configure = syscon_clock_frg_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct clk *const nxp_syscon_frg_children_##inst[] =             \
		CLOCK_INST_GET_DEPS(inst);                                     \
                                                                               \
	const struct syscon_clock_frg_config nxp_syscon_frg_##inst = {         \
		.children = nxp_syscon_frg_children_##inst,                    \
		.child_count = ARRAY_SIZE(nxp_syscon_frg_children_##inst),     \
		.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, NULL,                                       \
			     &nxp_syscon_frg_##inst,                           \
			     &nxp_syscon_frg_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
