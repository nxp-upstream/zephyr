/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/cache_device.h>
#include <zephyr/cache_info.h>

int main(void)
{
    printf("Hello World! %s\n", CONFIG_BOARD);

    /* If the Cache64 device is available and enabled in DT, show info */
	const struct device *cache64 = DEVICE_DT_GET(DT_NODELABEL(cache64));

    if (!device_is_ready(cache64)) {
        printf("cache64 device not ready\n");
        return 0;
    }

    /* Enable Cache64 */
    cache_device_enable(cache64);

    /* Query and print cache information */
    struct cache_info ci;
    int rc = cache_device_get_info(cache64, &ci);
    if (rc == 0) {
        printf("cache64 info: level=%u type=%u line=%luB ways=%lu sets=%lu size=%luB attrs=0x%08lx\n",
               (unsigned)ci.cache_level,
               (unsigned)ci.cache_type,
               (unsigned long)ci.line_size,
               (unsigned long)ci.ways,
               (unsigned long)ci.sets,
               (unsigned long)ci.size,
               (unsigned long)ci.attributes);
    } else {
        printf("cache_device_get_info() returned %d\n", rc);
    }

	return 0;
}
