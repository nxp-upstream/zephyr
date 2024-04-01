/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

/*
 * Call initialization functions for all clock nodes
 */
static int clock_mgmt_init(void)
{
	STRUCT_SECTION_FOREACH(clock_init, init_entry) {
		if (init_entry->init_fn) {
			init_entry->init_fn(init_entry->clk);
		}
	}
	return 0;
}

/* Initialize clocks at early boot */
SYS_INIT(clock_mgmt_init, EARLY, 0);
