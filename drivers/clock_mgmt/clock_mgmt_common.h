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
 */
void clock_mgmt_install_forward_cb(const struct clk *clk);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_CLOCK_MGMT_CLOCK_MGMT_COMMON_H_ */
