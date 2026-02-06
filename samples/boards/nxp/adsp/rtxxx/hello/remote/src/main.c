/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>

int main(void)
{
	int ret;

	printk("[DSP] Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;
}
