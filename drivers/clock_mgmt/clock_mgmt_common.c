/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

/**
 * @brief Helper to forward a clock callback to all children nodes
 *
 * Helper function to forward a clock callback. This function will fire a
 * callback for all child clocks, effectively forwarding the clock callback
 * notification to any subscribers for this clock.
 *
 * @return 0 on success
 */
int clock_mgmt_forward_cb(const struct clk *clk, const struct clk *parent)
{
	const struct clk *child = (const struct clk *)clk->children;
	ARG_UNUSED(parent);

	while (child) {
		clock_notify(child, clk);
		child++;
	}
	return 0;
}
