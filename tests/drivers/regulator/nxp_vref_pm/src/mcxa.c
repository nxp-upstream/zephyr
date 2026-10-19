/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/ztest.h>

#define FSL_FEATURE_VREF_HAS_LPBGEN 0
#define FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER 0
#define FSL_FEATURE_VREF_SUPPORT_2V5 1
#define FSL_FEATURE_SOC_SPC_COUNT 1
#define nxp_vref nxp_vref_mcxa
#include "regulator_nxp_vref.c"

static VREF_Type regs = {.CSR = VREF_CSR_VREFST_MASK};
static SPC_Type spc = {.ACTIVE_CFG1 = BIT(7)};
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
	.spc = &spc,
};

PM_DEVICE_DEFINE(vref_mcxa_pm, regulator_nxp_vref_pm_action);
DEVICE_DEFINE(vref_mcxa, "vref_mcxa", regulator_nxp_vref_init, PM_DEVICE_GET(vref_mcxa_pm),
	      &data, &config, POST_KERNEL, 80, &api);

ZTEST(vref_mcxa, test_discrete_voltage_selections)
{
	const struct device *dev = DEVICE_GET(vref_mcxa);
	const int32_t voltages[] = {1000000, 1250000, 1500000, 1800000, 2048000, 2500000};
	int32_t value;

	zassert_true(device_is_ready(dev));
	zassert_equal(spc.ACTIVE_CFG1, BIT(7) | BIT(0));
	zassert_equal(regulator_count_voltages(dev), ARRAY_SIZE(voltages));
	for (unsigned int i = 0U; i < ARRAY_SIZE(voltages); i++) {
		zassert_ok(regulator_list_voltage(dev, i, &value));
		zassert_equal(value, voltages[i]);
		zassert_ok(regulator_set_voltage(dev, value, value));
		zassert_equal(regs.UTRIM & VREF_UTRIM_TRIM2V1_MASK, i);
		zassert_ok(regulator_get_voltage(dev, &value));
		zassert_equal(value, voltages[i]);
	}
	zassert_ok(regulator_set_voltage(dev, 1700000, 2100000));
	zassert_ok(regulator_get_voltage(dev, &value));
	zassert_equal(value, 1800000);
	zassert_equal(regulator_set_voltage(dev, 1600000, 1700000), -EINVAL);
	zassert_equal(regulator_list_voltage(dev, ARRAY_SIZE(voltages), &value), -EINVAL);
	zassert_equal(pm_device_runtime_usage(dev), 0);
}

ZTEST(vref_mcxa, test_bias_and_cleared_configuration_replay)
{
	const struct device *dev = DEVICE_GET(vref_mcxa);

	zassert_ok(pm_device_runtime_disable(dev));
	zassert_ok(regulator_set_mode(dev, NXP_VREF_MODE_HIGH_POWER));
	zassert_ok(regulator_enable(dev));
	zassert_equal(regs.CSR & NXP_VREF_ENABLE_MASK, NXP_VREF_ENABLE_MASK);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(regs.CSR & NXP_VREF_ENABLE_MASK, 0U);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF));
	regs.CSR = VREF_CSR_VREFST_MASK | NXP_VREF_CONFIG_MASK;
	spc.ACTIVE_CFG1 = BIT(7);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
	zassert_equal(spc.ACTIVE_CFG1, BIT(7) | BIT(0), "SPC supply was not restored");
	zassert_equal(regs.CSR & NXP_VREF_CONFIG_MASK, 0U, "false DT options were not restored");
	zassert_equal(regs.CSR & NXP_VREF_ENABLE_MASK, 0U);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(regs.CSR & NXP_VREF_ENABLE_MASK, NXP_VREF_ENABLE_MASK);
	zassert_ok(regulator_disable(dev));
	zassert_equal(regs.CSR & NXP_VREF_ENABLE_MASK, 0U);
	zassert_ok(pm_device_runtime_enable(dev));
}

ZTEST_SUITE(vref_mcxa, NULL, NULL, NULL, NULL, NULL);
