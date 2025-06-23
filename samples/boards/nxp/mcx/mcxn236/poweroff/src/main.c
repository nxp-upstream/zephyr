/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/drivers/counter.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys_clock.h>

const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(lptmr0));

static void top_handler(const struct device *dev, void *user_data)
{
	int ret;

	ret = counter_stop(dev);

	if (ret == 0)
	{
		printf("Wake up success\n");
	}
	else
	{
		return;
	}
}

int main(void)
{
	int ret;
	struct counter_top_cfg top_cfg = {
		.ticks = counter_us_to_ticks(dev, 3000000),
		.callback = top_handler,
		.user_data = NULL,
		.flags = 0,
	};

	if (!device_is_ready(dev)) {
		printf("Counter device not ready\n");
		return 0;
	}

	ret = counter_set_top_value(dev, &top_cfg);

	ret = counter_start(dev);
	if (ret < 0) {
		printf("Could not start counter (%d)\n", ret);
		return 0;
	}

	printf("Will wakeup after 3 seconds\n");
	printf("Powering off\n");

	sys_poweroff();

	return 0;
}
