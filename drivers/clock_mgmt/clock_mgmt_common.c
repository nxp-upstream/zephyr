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

/* Callback implementation to forward a callback to subscribers to this
 * clock's rate change notifications
 */
void clock_mgmt_forward_cb(void *clk_obj)
{
	const struct clk *clk = clk_obj;

	clock_fire_callbacks(clk);
}

/**
 * @brief Helper to install a standard callback on the parent's clock
 *
 * Helper function to install a callback on a clock's parent during
 * init. This function requires that the first element in the `config`
 * structure for the given clock be a pointer to its parent clock,
 * and the first element in the `data` structure be a
 * `struct clock_mgmt_callback`.
 *
 * The function will install a callback on the parent clock which
 * simply fires any callbacks registered for the current clock, effectively
 * forwarding the clock callback notification to any subscribers for this clock.
 * @return -EINVAL if parameters are invalid
 * @return 0 on success
 */
void clock_mgmt_install_forward_cb(const struct clk *clk)
{
	const struct clk *parent = clk->config;
	struct clock_mgmt_callback *cb = (struct clock_mgmt_callback *)&clk->data;

	/* Setup callback object */
	clock_init_callback(cb, clock_mgmt_forward_cb, (void *)clk);

	clock_register_callback(parent, cb);
}
