/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_CLOCK_MGMT_CLOCK_MGMT_COMMON_H_
#define ZEPHYR_DRIVERS_CLOCK_MGMT_CLOCK_MGMT_COMMON_H_

#include <zephyr/drivers/clock_mgmt/clock_driver.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Helper to forward a clock callback to all children nodes
 *
 * Helper function to forward a clock callback. This function will fire a
 * callback for all child clocks, effectively forwarding the clock callback
 * notification to any subscribers for this clock.
 *
 * @return 0 on success
 */
int clock_mgmt_forward_cb(const struct clk *clk, const struct clk *parent);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_CLOCK_MGMT_CLOCK_MGMT_COMMON_H_ */
