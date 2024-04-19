/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_rtcclk

struct syscon_rtcclk_config {
	uint16_t add_factor;
	uint8_t mask_offset;
	uint8_t mask_width;
	const struct clk *parent;
	volatile uint32_t *reg;
};


int syscon_clock_rtcclk_get_rate(const struct clk *clk)
{
	const struct syscon_rtcclk_config *config = clk->hw_data;
	int parent_rate = clock_get_rate(config->parent);
	uint8_t div_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint32_t div_factor = (*config->reg & div_mask) + config->add_factor;

	if (parent_rate <= 0) {
		return parent_rate;
	}

	/* Calculate divided clock */
	return parent_rate / div_factor;
}

int syscon_clock_rtcclk_configure(const struct clk *clk, const void *div)
{
	const struct syscon_rtcclk_config *config = clk->hw_data;
	int parent_rate = clock_get_rate(config->parent);
	uint8_t div_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint32_t div_val = ((uint32_t)div) - config->add_factor;
	uint32_t div_raw = FIELD_PREP(div_mask, div_val);

	uint32_t new_rate = parent_rate / ((uint32_t)div);

	clock_notify_children(clk, new_rate);
	(*config->reg) = ((*config->reg) & ~div_mask) | div_raw;
	return 0;
}

int syscon_clock_rtcclk_notify(const struct clk *clk, const struct clk *parent,
			       uint32_t parent_rate)
{
	const struct syscon_rtcclk_config *config = clk->hw_data;
	uint8_t div_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint32_t div_factor = (*config->reg & div_mask) + config->add_factor;


	return clock_notify_children(clk, (parent_rate / div_factor));
}

const struct clock_driver_api nxp_syscon_rtcclk_api = {
	.get_rate = syscon_clock_rtcclk_get_rate,
	.configure = syscon_clock_rtcclk_configure,
	.notify = syscon_clock_rtcclk_notify,
};

#define NXP_RTCCLK_DEFINE(inst)                                                \
	const struct syscon_rtcclk_config nxp_syscon_rtcclk_##inst = {         \
		.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = DT_INST_REG_SIZE(inst),                          \
		.mask_offset = DT_INST_PROP(inst, offset),                     \
		.add_factor = DT_INST_PROP(inst, add_factor),                  \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     &nxp_syscon_rtcclk_##inst,                        \
			     &nxp_syscon_rtcclk_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_RTCCLK_DEFINE)
