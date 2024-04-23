/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#define DT_DRV_COMPAT clock_output

int clock_output_get_rate(const struct clk *clk)
{
	const struct clk *parent = (const struct clk *)clk->hw_data;

	return clock_get_rate(parent);
}

int clock_output_configure(const struct clk *clk, const void *rate)
{
	const struct clk *parent = (const struct clk *)clk->hw_data;

	return clock_set_rate(parent, (uint32_t)rate, clk);
}

int clock_output_notify(const struct clk *clk, const struct clk *parent,
			     uint32_t parent_rate)
{
	return clock_notify_children(clk, parent_rate);
}

const struct clock_driver_api clock_output_api = {
	.get_rate = clock_output_get_rate,
	.notify = clock_output_notify,
#if defined(CONFIG_CLOCK_MGMT_SET_RATE)
	.configure = clock_output_configure,
#endif
};

#define CLOCK_OUTPUT_DEFINE(inst)                                              \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     CLOCK_DT_GET(DT_INST_PARENT(inst)),               \
			     &clock_output_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_OUTPUT_DEFINE)
