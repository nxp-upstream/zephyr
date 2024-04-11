/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>
#include <soc.h>

#define DT_DRV_COMPAT fixed_clock_source

int clock_source_get_rate(const struct clk *clk)
{
	return (int)clk->hw_data;
}

const struct clock_driver_api clock_source_api = {
	.get_rate = clock_source_get_rate,
};

#define CLOCK_SOURCE_DEFINE(inst)                                              \
	CLOCK_DT_INST_DEFINE(inst,                                             \
			     ((uint32_t *)DT_INST_PROP(inst, frequency)),      \
			     &clock_source_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_SOURCE_DEFINE)
