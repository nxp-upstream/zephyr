/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#define FSL_FEATURE_VREF_HAS_TRIM2V1 0
#define FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER 0
#define nxp_vref nxp_vref_factory
#include "regulator_nxp_vref.c"

static VREF_Type regs = {.CSR = VREF_CSR_VREFST_MASK, .UTRIM = 0x1100U};
static struct regulator_nxp_vref_data data;
static const struct regulator_nxp_vref_config config = {
	.common = {
		.min_uv = INT32_MIN, .max_uv = INT32_MAX,
		.init_uv = INT32_MIN, .init_ua = INT32_MIN,
		.initial_mode = NXP_VREF_MODE_HIGH_POWER,
		.flags = REGULATOR_ACTIVE_DISCHARGE_SET_BITS(REGULATOR_ACTIVE_DISCHARGE_DEFAULT),
	},
	.base = &regs,
	.bg_start_time = 1,
	.buf_start_delay = 1,
};

PM_DEVICE_DEFINE(vref_factory_pm, regulator_nxp_vref_pm_action);
DEVICE_DEFINE(vref_factory, "vref_factory", regulator_nxp_vref_init,
	      PM_DEVICE_GET(vref_factory_pm), &data, &config, POST_KERNEL, 80, &api);

ZTEST(vref_factory, test_factory_trim_and_consumer_override)
{
	const struct device *dev = DEVICE_GET(vref_factory);
	int32_t voltage;

	zassert_true(device_is_ready(dev));
	zassert_equal(regs.UTRIM, 0x1100U, "init overwrote factory trim");
	zassert_ok(pm_device_runtime_disable(dev));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF));
	regs.UTRIM = 0x1200U;
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
	zassert_equal(regs.UTRIM, 0x1200U, "TURN_ON overwrote reset-loaded trim");
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));

	zassert_ok(regulator_list_voltage(dev, 32U, &voltage));
	zassert_ok(regulator_set_voltage(dev, voltage, voltage));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF));
	regs.UTRIM = 0x1203U;
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
	zassert_equal(regs.UTRIM, 0x2003U, "consumer trim or unrelated bits lost");
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	zassert_ok(pm_device_runtime_enable(dev));
}

ZTEST_SUITE(vref_factory, NULL, NULL, NULL, NULL, NULL);
