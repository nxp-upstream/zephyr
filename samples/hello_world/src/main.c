/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/drivers/clock_mgmt/clock_driver.h>

int main(void)
{
#ifdef CONFIG_CLOCK_MGMT
	const struct clk *clk = CLOCK_DT_GET(DT_NODELABEL(frgctrl0_mul));
	const struct clk *core = CLOCK_DT_GET(DT_NODELABEL(ahbclkdiv));

	printf("FRG0 Clock rate was %d\n", clock_get_rate(clk));
	printf("Core Clock rate was %d\n", clock_get_rate(core));
#endif
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;
}
