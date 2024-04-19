/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_flexfrg

struct syscon_clock_frg_config {
	const struct clk *parent;
	volatile uint32_t *reg;
};

#define SYSCON_FLEXFRGXCTRL_DIV_MASK 0xFF
#define SYSCON_FLEXFRGXCTRL_MULT_MASK 0xFF00

/* Rate calculation helper */
static uint32_t syscon_clock_frg_calc_rate(uint64_t parent_rate, uint32_t mult)
{
	/*
	 * Calculate rate. We will use 64 bit integers for this division.
	 * DIV value must be 256, no need to read it
	 */
	return (uint32_t)((parent_rate * ((uint64_t)SYSCON_FLEXFRGXCTRL_MULT_MASK + 1ULL)) /
		(mult + SYSCON_FLEXFRGXCTRL_MULT_MASK + 1UL));
}

int syscon_clock_frg_get_rate(const struct clk *clk)
{
	const struct syscon_clock_frg_config *config = clk->hw_data;
	int parent_rate = clock_get_rate(config->parent);
	uint32_t frg_mult;

	if (parent_rate <= 0) {
		return parent_rate;
	}

	frg_mult = FIELD_GET(SYSCON_FLEXFRGXCTRL_MULT_MASK, (*config->reg));
	return syscon_clock_frg_calc_rate(parent_rate, frg_mult);
}

int syscon_clock_frg_configure(const struct clk *clk, const void *mult)
{
	const struct syscon_clock_frg_config *config = clk->hw_data;
	uint32_t mult_val = FIELD_PREP(SYSCON_FLEXFRGXCTRL_MULT_MASK, ((uint32_t)mult));
	int parent_rate = clock_get_rate(config->parent);
	uint32_t new_rate = syscon_clock_frg_calc_rate(parent_rate, (uint32_t)mult);

	clock_notify_children(clk, new_rate);
	/* DIV field should always be 0xFF */
	(*config->reg) = mult_val | SYSCON_FLEXFRGXCTRL_DIV_MASK;
	return 0;
}

int syscon_clock_frg_notify(const struct clk *clk, const struct clk *parent,
			    uint32_t parent_rate)
{
	const struct syscon_clock_frg_config *config = clk->hw_data;
	uint32_t new_rate;
	uint32_t frg_mult;

	frg_mult = FIELD_GET(SYSCON_FLEXFRGXCTRL_MULT_MASK, (*config->reg));
	new_rate = syscon_clock_frg_calc_rate(parent_rate, frg_mult);
	return clock_notify_children(clk, new_rate);
}

const struct clock_driver_api nxp_syscon_frg_api = {
	.get_rate = syscon_clock_frg_get_rate,
	.configure = syscon_clock_frg_configure,
	.notify = syscon_clock_frg_notify,
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct syscon_clock_frg_config nxp_syscon_frg_##inst = {         \
		.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     &nxp_syscon_frg_##inst,                           \
			     &nxp_syscon_frg_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_SYSCON_CLOCK_DEFINE)
