/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

struct clk_child_data {
	const struct clk *const *children;
	uint8_t child_count;
};

/**
 * @brief Helper to forward a clock callback to all children nodes
 *
 * Helper function to forward a clock callback. This function will fire a
 * callback for all child clocks, effectively forwarding the clock callback
 * notification to any subscribers for this clock.
 *
 * @note In order for this function to work as expected, the first field
 * in the clock's private configuration data must be a pointer to an array
 * of children clocks, and the second field must be a 1 byte count of
 * clock children, like so:
 * @code{.c}
 *     struct priv_clk_config {
 *             const struct clk *const children;
 *             const uint8_t child_count;
 *             ...
 *     };
 * @endcode
 *
 * @return 0 on success
 */
int clock_mgmt_forward_cb(const struct clk *clk, const struct clk *parent)
{
	const struct clk_child_data *children = clk->config;
	ARG_UNUSED(parent);

	for (uint8_t i = 0; i < children->child_count; i++) {
		clock_notify(children->children[i], clk);
	}
	return 0;
}
