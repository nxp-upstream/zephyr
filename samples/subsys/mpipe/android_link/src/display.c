/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>

#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display);

int display_setup(const struct device *const display_dev)
{
	int ret;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found\n", display_dev->name);
		return -ENODEV;
	}

	LOG_INF("Display device: %s\n", display_dev->name);

	ret = display_set_pixel_format(display_dev, SCR_FORMAT);
	if (ret < 0) {
		LOG_ERR("Unable to set pixel format");
		return ret;
	}

	return display_blanking_off(display_dev);
}
