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

/**
 * @brief Helper to install a callback on the parent's clock
 *
 * Helper function to install a callback on a clock's parent during
 * init. This function requires that the first element in the `config`
 * structure for the given clock be a pointer to its parent clock,
 * and the first element in the `data` structure be an initialized
 * `struct clock_mgmt_callback`.
 *
 * The function will install a callback on the parent clock, using the
 * `struct clock_mgmt_callback`.
 * @return -EINVAL if parameters are invalid
 * @return 0 on success
 */
int clock_mgmt_install_parent_cb(const struct clk *clk)
{
	struct clk *parent = clk->config;
	struct clock_mgmt_callback *cb = &clk->data;

	return clock_register_callback(parent, cb);
}
