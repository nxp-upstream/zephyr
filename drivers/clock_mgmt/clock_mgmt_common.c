/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt.h>

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
	const struct clk *const *child = clk->children;
	ARG_UNUSED(parent);

	while (*child) {
		clock_notify(*child, clk);
		child++;
	}
	return 0;
}

/*
 * Common handler used to notify clock consumers of clock events.
 * This handler is used by the clock management subsystem to notify consumers
 * via callback that a parent was reconfigured
 */
int clock_mgmt_notify_consumer(const struct clk *clk, const struct clk *parent)
{
	const struct clock_mgmt *clock_mgmt = clk->hw_data;
	struct clock_mgmt_callback *callback = clock_mgmt->callback;

	if (!callback->clock_callback) {
		/* No callback installed */
		return 0;
	}

	/* Call clock notification callback */
	for (uint8_t i = 0; i < clock_mgmt->output_count; i++) {
		if (parent == clock_mgmt->outputs[i]) {
			/* Issue callback */
			callback->clock_callback(i, callback->user_data);
		}
	}
	return 0;
}

/* API structure used by clock management code for clock consumers */
const struct clock_mgmt_clk_api clock_consumer_api = {
	.notify = clock_mgmt_notify_consumer,
};

/**
 * @brief Set new clock state
 *
 * Sets new clock state. This function will apply a clock state as defined
 * in devicetree. Clock states can configure clocks systemwide, or only for
 * the relevant peripheral driver. Clock states are defined as clock-state-"n"
 * properties of the devicetree node for the given driver.
 * @param clk_cfg Clock management structure
 * @param state_idx Clock state index
 * @return -EINVAL if parameters are invalid
 * @return -ENOENT if state index could not be found
 * @return -ENOSYS if clock does not implement configure API
 * @return -EIO if state could not be set
 * @return -EBUSY if clocks cannot be modified at this time
 * @return 0 on success
 */
int clock_mgmt_apply_state(const struct clock_mgmt *clk_cfg,
			   uint8_t state_idx)
{
	const struct clock_mgmt_state *state;
	int ret;

	if (!clk_cfg) {
		return -EINVAL;
	}

	if (clk_cfg->state_count <= state_idx) {
		return -ENOENT;
	}

	state = clk_cfg->states[state_idx];

	for (uint8_t i = 0; i < state->num_clocks; i++) {
		ret = clock_configure(state->clocks[i],
				      state->clock_config_data[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
