 /*
 * Copyright 2025 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_spc_regulator

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>

#include <fsl_spc.h>

struct regulator_nxp_spc_config {
    struct regulator_common_config common;
    SPC_Type *base;
};

struct regulator_nxp_spc_data {
	struct regulator_common_data common;
}

static int regulator_nxp_spc_pm_action(const struct device *dev,
                 enum pm_device_action action)
{
    switch (action)
    {
    case PM_DEVICE_ACTION_TURN_ON:
        /* SPC specific turn on actions can be added here if needed */
        break;

    case PM_DEVICE_ACTION_TURN_OFF:
        /* SPC specific turn off actions can be added here if needed */
        break;

    case PM_DEVICE_ACTION_SUSPEND:
        /* SPC specific suspend actions can be added here if needed */
        break;

    case PM_DEVICE_ACTION_RESUME:
        /* SPC specific resume actions can be added here if needed */
        break;
    default:
        break;
    }

    return 0;
}

static int regulator_nxp_spc_init(const struct device *dev)
{
    const struct regulator_nxp_spc_config *config = dev->config;
    struct regulator_nxp_spc_data *data = dev->data;

    regulator_common_data_init(dev);



    return pm_device_driver_init(dev, regulator_nxp_spc_pm_action);
}
