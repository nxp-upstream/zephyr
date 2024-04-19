/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>
#include <stdlib.h>

#define DT_DRV_COMPAT nxp_syscon_clock_mux

struct syscon_clock_mux_config {
	uint8_t mask_width;
	uint8_t mask_offset;
	uint8_t src_count;
	volatile uint32_t *reg;
	const struct clk *parents[];
};

int syscon_clock_mux_get_rate(const struct clk *clk)
{
	const struct syscon_clock_mux_config *config = clk->hw_data;
	uint8_t mux_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	if (sel > config->src_count) {
		return -EIO;
	}

	return clock_get_rate(config->parents[sel]);
}

int syscon_clock_mux_configure(const struct clk *clk, const void *mux)
{
	const struct syscon_clock_mux_config *config = clk->hw_data;

	uint8_t mux_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint32_t mux_val = FIELD_PREP(mux_mask, ((uint32_t)mux));

	if (((uint32_t)mux) > config->src_count) {
		return -EINVAL;
	}

	clock_notify_children(clk, clock_get_rate(config->parents[(uint32_t)mux]));
	(*config->reg) = ((*config->reg) & ~mux_mask) | mux_val;
	return 0;
}

int syscon_clock_mux_notify(const struct clk *clk, const struct clk *parent,
			    uint32_t parent_rate)
{
	const struct syscon_clock_mux_config *config = clk->hw_data;
	uint8_t mux_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	if (sel > config->src_count) {
		return -EINVAL;
	}

	/*
	 * Read div reg, and if index matches parent index we should notify
	 * children
	 */
	if (config->parents[sel] == parent) {
		clock_notify_children(clk, parent_rate);
	}

	return 0;
}

int syscon_clock_mux_round_rate(const struct clk *clk, uint32_t rate)
{
	const struct syscon_clock_mux_config *config = clk->hw_data;
	int cand_rate;
	int best_delta = INT32_MAX;
	int best_rate = 0;
	uint8_t idx = 0;

	/*
	 * Select a parent source based on the one able to
	 * provide the rate closest to what was requested by the
	 * caller
	 */
	while ((idx < config->src_count) && (best_delta > 0)) {
		cand_rate = clock_round_rate(config->parents[idx], rate);
		if (abs(cand_rate - rate) < best_delta) {
			best_rate = cand_rate;
			best_delta = abs(cand_rate - rate);
		}
	}

	return best_rate;
}

int syscon_clock_mux_set_rate(const struct clk *clk, uint32_t rate)
{
	const struct syscon_clock_mux_config *config = clk->hw_data;
	int cand_rate;
	int best_rate;
	int best_delta = INT32_MAX;
	uint32_t mux_val;
	uint8_t idx = 0;
	uint8_t best_idx = 0;
	uint8_t mux_mask = GENMASK((config->mask_width +
				   config->mask_offset - 1),
				   config->mask_offset);

	/*
	 * Select a parent source based on the one able to
	 * provide the rate closest to what was requested by the
	 * caller
	 */
	while ((idx < config->src_count) && (best_delta > 0)) {
		cand_rate = clock_round_rate(config->parents[idx], rate);
		if (abs(cand_rate - rate) < best_delta) {
			best_idx = idx;
			best_delta = abs(cand_rate - rate);
		}
	}

	/* Now set the clock rate for the best parent */
	best_rate = clock_set_rate(config->parents[best_idx], rate);
	clock_notify_children(clk, best_rate);
	mux_val = FIELD_PREP(mux_mask, best_idx);
	(*config->reg) = ((*config->reg) & ~mux_mask) | mux_val;

	return best_rate;
}

const struct clock_driver_api nxp_syscon_mux_api = {
	.get_rate = syscon_clock_mux_get_rate,
	.configure = syscon_clock_mux_configure,
	.notify = syscon_clock_mux_notify,
#if defined(CONFIG_CLOCK_MGMT_SET_RATE)
	.round_rate = syscon_clock_mux_round_rate,
	.set_rate = syscon_clock_mux_set_rate,
#endif
};

#define GET_MUX_INPUT(node_id, prop, idx)                                      \
	CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct syscon_clock_mux_config nxp_syscon_mux_##inst = {         \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = (uint8_t)DT_INST_REG_SIZE(inst),                 \
		.src_count = DT_INST_PROP_LEN(inst, input_sources),            \
		.parents = {                                                   \
			DT_INST_FOREACH_PROP_ELEM(inst, input_sources,         \
						GET_MUX_INPUT)                 \
		},                                                             \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     &nxp_syscon_mux_##inst,                           \
			     &nxp_syscon_mux_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_SYSCON_CLOCK_DEFINE)
