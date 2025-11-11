/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/cache_device.h>

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	const struct device *cache64 = DEVICE_DT_GET(DT_NODELABEL(cache64));

    if (!device_is_ready(cache64)) {
        return;
    }

    /* Enable Cache64 */
    cache_device_enable(cache64);

    /* Get cache information */
    struct cache_device_info info;
    cache_device_get_info(cache64, &info);

    printk("Cache64 - Type: %s, Purpose: %s, Size: %u bytes\n",
           (info.cache_type & CACHE_DEVICE_TYPE_UNIFIED) ? "Unified" : "Other",
           (info.cache_purpose & CACHE_DEVICE_PURPOSE_EXTERNAL_FLASH) ? "External Flash" : "Other",
           info.size);

	return 0;
}
