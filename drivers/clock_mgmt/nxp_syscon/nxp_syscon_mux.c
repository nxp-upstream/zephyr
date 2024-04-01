/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT nxp_syscon_clock_mux

struct syscon_clock_mux_config {
	const struct clk **parents;
	volatile uint32_t *reg;
	uint8_t mask_width:3;
	uint8_t mask_offset:5;
};

struct syscon_clock_mux_data {
	struct clock_mgmt_callback cb;
};

int syscon_clock_mux_get_rate(const struct clk *clk)
{
	const struct syscon_clock_mux_config *config = clk->config;
	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	return clock_get_rate(config->parents[sel]);
}

int syscon_clock_mux_configure(const struct clk *clk, void *mux)
{
	const struct syscon_clock_mux_config *config = clk->config;
	struct syscon_clock_mux_data *data = clk->data;

	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint8_t current_sel = ((*config->reg) & mux_mask) >> config->mask_offset;
	uint32_t mux_val = FIELD_PREP(mux_mask, ((uint32_t)mux));


	/* Remove current clock callback registration */
	clock_unregister_callback(config->parents[current_sel], &data->cb);
	(*config->reg) = ((*config->reg) & ~mux_mask) | mux_val;
	/* Register for new callback */
	clock_unregister_callback(config->parents[((uint32_t)mux)], &data->cb);
	return 0;
}

void syscon_clock_mux_cb(void *clk_obj)
{
	const struct clk *clk = clk_obj;

	/* Forward callback to children */
	clock_fire_callbacks(clk);
}

void syscon_clock_mux_init(const struct clk *clk)
{
	const struct syscon_clock_mux_config *config = clk->config;
	struct syscon_clock_mux_data *data = clk->data;
	uint8_t mux_mask = GENMASK(config->mask_offset,
				   (config->mask_width +
				   config->mask_offset - 1));
	uint8_t sel = ((*config->reg) & mux_mask) >> config->mask_offset;

	/* Init callback */
	clock_init_callback(&data->cb, syscon_clock_mux_cb, (void*)clk);
	/* Register callback to currently selected parent */
	clock_register_callback(config->parents[sel], &data->cb);
}

const struct clock_driver_api nxp_syscon_mux_api = {
	.get_rate = syscon_clock_mux_get_rate,
	.configure = syscon_clock_mux_configure,
};

#define GET_MUX_INPUT(node_id, prop, idx)                                      \
	CLOCK_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define NXP_SYSCON_CLOCK_DEFINE(inst)                                          \
	const struct clk *nxp_syscon_mux_parents_##inst[] = {                  \
		DT_INST_FOREACH_PROP_ELEM(inst, input_sources, GET_MUX_INPUT)  \
	};                                                                     \
	const struct syscon_clock_mux_config nxp_syscon_mux_##inst = {         \
	 	.parents = nxp_syscon_mux_parents_##inst,                      \
		.reg = (volatile uint32_t *)DT_INST_REG_ADDR(inst),            \
		.mask_width = (uint8_t)DT_INST_REG_SIZE(inst),                 \
	};                                                                     \
	struct syscon_clock_mux_data nxp_syscon_mux_data_##inst;               \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, syscon_clock_mux_init,                      \
			     &nxp_syscon_mux_data_##inst,                      \
			     &nxp_syscon_mux_##inst,                           \
			     &nxp_syscon_mux_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_SYSCON_CLOCK_DEFINE)
