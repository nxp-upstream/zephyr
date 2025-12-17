/*
 * Copyright (c) 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT test_power_domain

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/device.h>

static int pd_test_init(const struct device *dev)
{
    return 0;
}

static int pd_test_action(const struct device *dev,
                        enum pm_device_action action)
{
    return 0;
};

#define PM_DOMAIN_TYPE COND_CODE_1(CONFIG_TEST_PM_DEVICE_ISR_SAFE, (PM_DEVICE_ISR_SAFE), (0))

PM_DEVICE_DT_INST_DEFINE(0, pd_test_action, PM_DOMAIN_TYPE);

DEVICE_DT_INST_DEFINE(0, pd_test_init, PM_DEVICE_DT_INST_GET(0), NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
