/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_clock_div

struct syscon_clock_div_config {
	uint8_t mask_width;
	const struct clk *parent;
	volatile uint32_t *reg;
};


int syscon_clock_div_get_rate(const struct clk *clk)
{
	const struct syscon_clock_div_config *config = clk->hw_data;
	int parent_rate = clock_get_rate(config->parent);
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));

	if (parent_rate <= 0) {
		return parent_rate;
	}

	/* Calculate divided clock */
	return parent_rate / ((*config->reg & div_mask) + 1);
}

int syscon_clock_div_configure(const struct clk *clk, const void *div)
{
	const struct syscon_clock_div_config *config = clk->hw_data;
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));
	uint32_t div_val = (((uint32_t)div) - 1) & div_mask;
	int parent_rate = clock_get_rate(config->parent);
	uint32_t new_rate = (parent_rate / ((uint32_t)div));

	clock_notify_children(clk, new_rate);
	(*config->reg) = ((*config->reg) & ~div_mask) | div_val;
	return 0;
}

int syscon_clock_div_notify(const struct clk *clk, const struct clk *parent,
			    uint32_t parent_rate)
{
	const struct syscon_clock_div_config *config = clk->hw_data;
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));
	uint32_t new_rate = (parent_rate / ((*config->reg & div_mask) + 1));

	return clock_notify_children(clk, new_rate);
}

int syscon_clock_div_round_rate(const struct clk *clk, uint32_t rate)
{
	const struct syscon_clock_div_config *config = clk->hw_data;
	int parent_rate = clock_round_rate(config->parent, rate);
	int div_val = MAX((parent_rate / rate), 1) - 1;
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));

	return parent_rate / ((div_val & div_mask) + 1);
}

int syscon_clock_div_set_rate(const struct clk *clk, uint32_t rate)
{
	const struct syscon_clock_div_config *config = clk->hw_data;
	int parent_rate = clock_set_rate(config->parent, rate);
	int div_val = MAX((parent_rate / rate), 1);
	uint8_t div_mask = GENMASK(0, (config->mask_width - 1));
	uint32_t output_rate = parent_rate / ((div_val & div_mask) + 1);

	clock_notify_children(clk, output_rate);
	(*config->reg) = ((*config->reg) & ~div_mask) | ((div_val - 1) & div_mask);
	return output_rate;
}

const struct clock_driver_api nxp_syscon_div_api = {
	.get_rate = syscon_clock_div_get_rate,
	.configure = syscon_clock_div_configure,
	.notify = syscon_clock_div_notify,
#if defined(CONFIG_CLOCK_MGMT_SET_RATE)
	.round_rate = syscon_clock_div_round_rate,
	.set_rate = syscon_clock_div_set_rate,
#endif
};

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct syscon_clock_div_config nxp_syscon_div_##inst = {         \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = (uint8_t)DT_INST_REG_SIZE(inst),                 \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     &nxp_syscon_div_##inst,                           \
			     &nxp_syscon_div_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_SYSCON_CLOCK_DEFINE)
