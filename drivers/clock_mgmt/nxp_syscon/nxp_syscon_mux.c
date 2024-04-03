/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_clock_mux

struct syscon_clock_mux_config {
	const struct clk *const *parents;
	volatile uint32_t *reg;
	uint8_t mask_width;
	uint8_t mask_offset;
	uint8_t src_count;
};

int syscon_clock_mux_get_rate(const struct clk *clk)
{
	const struct syscon_clock_mux_config *config = clk->config;
	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	if (sel > config->src_count) {
		return -EIO;
	}

	return clock_get_rate(config->parents[sel]);
}

int syscon_clock_mux_configure(const struct clk *clk, void *mux)
{
	const struct syscon_clock_mux_config *config = clk->config;

	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint32_t mux_val = FIELD_PREP(mux_mask, ((uint32_t)mux));

	if (((uint32_t)mux) > config->src_count) {
		return -EINVAL;
	}

	(*config->reg) = ((*config->reg) & ~mux_mask) | mux_val;
	return 0;
}

int syscon_clock_mux_notify(const struct clk *clk, const struct clk *parent)
{
	const struct syscon_clock_mux_config *config = clk->config;
	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	if (sel > config->src_count) {
		return -EINVAL;
	}

	/*
	 * Read div reg, and if index matches parent index we should notify
	 * children
	 */
	if (config->parents[sel] == parent) {
		clock_notify_children(clk);
	}

	return 0;
}

const struct clock_driver_api nxp_syscon_mux_api = {
	.get_rate = syscon_clock_mux_get_rate,
	.configure = syscon_clock_mux_configure,
	.notify = syscon_clock_mux_notify,
};

#define REG_MUX_CALLBACK(node_id, prop, idx, inst)                             \
	CLOCK_NOTIFY_REGISTER_INST(inst, DT_PHANDLE_BY_IDX(node_id, prop, idx));

#define GET_MUX_INPUT(node_id, prop, idx)                                      \
	CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct clk *const nxp_syscon_mux_parents_##inst[] = {            \
		DT_INST_FOREACH_PROP_ELEM(inst, input_sources, GET_MUX_INPUT)  \
	};                                                                     \
	DT_INST_FOREACH_PROP_ELEM_VARGS(inst, input_sources, REG_MUX_CALLBACK, \
					inst)                                  \
	const struct syscon_clock_mux_config nxp_syscon_mux_##inst = {         \
	 	.parents = nxp_syscon_mux_parents_##inst,                      \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = (uint8_t)DT_INST_REG_SIZE(inst),                 \
		.src_count = ARRAY_SIZE(nxp_syscon_mux_parents_##inst),        \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, NULL,                                       \
			     &nxp_syscon_mux_##inst,                           \
			     &nxp_syscon_mux_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
