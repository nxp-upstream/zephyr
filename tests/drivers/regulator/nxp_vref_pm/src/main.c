/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

/* Exercise the production implementation with RAM registers and no DT instances. */
#include "regulator_nxp_vref.c"

#define OUTPUT_BITS (VREF_CSR_HCBGEN_MASK | VREF_CSR_LPBGEN_MASK | \
		     VREF_CSR_LPBG_BUF_EN_MASK | VREF_CSR_BUF21EN_MASK)
#define CONFIG_BITS (VREF_CSR_ICOMPEN_MASK | VREF_CSR_CHOPEN_MASK | VREF_CSR_REGEN_MASK)

#define TEST_DEVICE(n)										\
	static VREF_Type registers_##n = {.CSR = VREF_CSR_VREFST_MASK};		\
	static struct regulator_nxp_vref_data data_##n;				\
	static const struct regulator_nxp_vref_config config_##n = {		\
		.common = {								\
			.min_uv = INT32_MIN, .max_uv = INT32_MAX,			\
			.init_uv = INT32_MIN, .init_ua = INT32_MIN,			\
			.initial_mode = NXP_VREF_MODE_HIGH_POWER,			\
			.flags = REGULATOR_ACTIVE_DISCHARGE_SET_BITS(		\
				REGULATOR_ACTIVE_DISCHARGE_DEFAULT),		\
		},									\
		.base = &registers_##n,						\
		.bg_start_time = 1, .buf_start_delay = 1,				\
		.current_compensation_en = true, .chop_oscillator_en = true,	\
		.internal_voltage_regulator_en = true,				\
	};										\
	PM_DEVICE_DEFINE(vref_pm_##n, regulator_nxp_vref_pm_action);		\
	DEVICE_DEFINE(vref_##n, "vref_" #n, regulator_nxp_vref_init,		\
		      PM_DEVICE_GET(vref_pm_##n), &data_##n, &config_##n,		\
		      POST_KERNEL, 80, &api)

TEST_DEVICE(0);
TEST_DEVICE(1);

static const struct device *const dev = DEVICE_GET(vref_0);
static const struct device *const other = DEVICE_GET(vref_1);

static void assert_state(enum pm_device_state expected)
{
	enum pm_device_state state;

	zassert_ok(pm_device_state_get(dev, &state));
	zassert_equal(state, expected);
}

static void power_cycle(void)
{
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U, "SUSPEND left the output enabled");
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF));
	registers_0.CSR = VREF_CSR_VREFST_MASK;
	registers_0.UTRIM = 0x2003U;
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U, "TURN_ON enabled an output");
	zassert_equal(registers_0.CSR & CONFIG_BITS, CONFIG_BITS);
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(dev));
	zassert_true(device_is_ready(other));
	registers_0.CSR |= VREF_CSR_VREFST_MASK;
	zassert_ok(pm_device_runtime_disable(dev));

	enum pm_device_state state;

	zassert_ok(pm_device_state_get(dev, &state));
	if (state == PM_DEVICE_STATE_OFF) {
		zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
		state = PM_DEVICE_STATE_SUSPENDED;
	}
	if (state == PM_DEVICE_STATE_SUSPENDED) {
		zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	}
	while (regulator_is_enabled(dev)) {
		zassert_ok(regulator_disable(dev));
	}
	zassert_ok(regulator_set_mode(dev, NXP_VREF_MODE_HIGH_POWER));
}

ZTEST(vref_pm, test_held_output_and_mode_survive_power_loss)
{
	zassert_ok(regulator_set_voltage(dev, 1600000, 1600000));
	zassert_ok(regulator_set_mode(dev, NXP_VREF_MODE_LOW_POWER));
	zassert_ok(regulator_enable(dev));
	zassert_ok(regulator_enable(dev));

	for (int i = 0; i < 2; i++) {
		power_cycle();
		zassert_equal(registers_0.CSR & OUTPUT_BITS, OUTPUT_BITS);
		zassert_equal(registers_0.CSR & VREF_CSR_HI_PWR_LV_MASK, 0U);
		zassert_equal(registers_0.UTRIM, 0x2006U, "trim or unrelated bits lost");
	}
	zassert_ok(regulator_disable(dev));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, OUTPUT_BITS);
	zassert_ok(regulator_disable(dev));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U);
}

ZTEST(vref_pm, test_disabled_output_is_not_replayed)
{
	power_cycle();
	zassert_false(regulator_is_enabled(dev));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U);
	zassert_ok(regulator_enable(dev));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, OUTPUT_BITS);
	zassert_not_equal(registers_0.CSR & VREF_CSR_HI_PWR_LV_MASK, 0U);
	zassert_ok(regulator_disable(dev));
}

ZTEST(vref_pm, test_runtime_reference_follows_regulator_lifetime)
{
	zassert_ok(pm_device_runtime_enable(dev));
	assert_state(PM_DEVICE_STATE_SUSPENDED);
	zassert_ok(regulator_enable(dev));
	zassert_ok(regulator_enable(dev));
	assert_state(PM_DEVICE_STATE_ACTIVE);
	zassert_equal(pm_device_runtime_usage(dev), 1);
	zassert_ok(regulator_disable(dev));
	assert_state(PM_DEVICE_STATE_ACTIVE);
	zassert_equal(registers_0.CSR & OUTPUT_BITS, OUTPUT_BITS);
	zassert_ok(regulator_disable(dev));
	assert_state(PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(dev), 0);
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U);
}

ZTEST(vref_pm, test_runtime_configuration_does_not_enable_output)
{
	int32_t voltage;

	zassert_ok(pm_device_runtime_enable(dev));
	zassert_ok(regulator_set_mode(dev, NXP_VREF_MODE_LOW_POWER));
	zassert_ok(regulator_set_voltage(dev, 1700000, 1700000));
	zassert_ok(regulator_get_voltage(dev, &voltage));
	zassert_equal(voltage, 1700000);
	assert_state(PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pm_device_runtime_usage(dev), 0);
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U);
}

ZTEST(vref_pm, test_manual_suspend_rejects_register_access)
{
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(regulator_set_mode(dev, UINT8_MAX), -EINVAL);
	zassert_equal(regulator_set_voltage(dev, 1, 1), -EINVAL);
	zassert_equal(regulator_set_mode(dev, NXP_VREF_MODE_LOW_POWER), -EBUSY);
	zassert_equal(regulator_enable(dev), -EBUSY);
	zassert_false(regulator_is_enabled(dev));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
}

ZTEST(vref_pm, test_instances_have_independent_replay_state)
{
	uint32_t other_csr = registers_1.CSR;
	uint32_t other_trim = registers_1.UTRIM;

	zassert_ok(regulator_set_voltage(dev, 1500000, 1500000));
	zassert_ok(regulator_enable(dev));
	power_cycle();
	zassert_equal(registers_1.CSR, other_csr);
	zassert_equal(registers_1.UTRIM, other_trim);
	zassert_false(regulator_is_enabled(other));
	zassert_ok(regulator_disable(dev));
}

ZTEST(vref_pm, test_enable_timeout_releases_runtime_reference)
{
	zassert_ok(pm_device_runtime_enable(dev));
	registers_0.CSR &= ~VREF_CSR_VREFST_MASK;
	zassert_equal(regulator_enable(dev), -ETIMEDOUT);
	zassert_false(regulator_is_enabled(dev));
	zassert_equal(registers_0.CSR & OUTPUT_BITS, 0U);
	zassert_equal(pm_device_runtime_usage(dev), 0);
	assert_state(PM_DEVICE_STATE_SUSPENDED);
	registers_0.CSR |= VREF_CSR_VREFST_MASK;
	zassert_ok(regulator_enable(dev));
	zassert_ok(regulator_disable(dev));
}

ZTEST(vref_pm, test_voltage_window_and_invalid_index)
{
	int32_t voltage;

	zassert_ok(regulator_set_voltage(dev, 1650000, 1750000));
	zassert_ok(regulator_get_voltage(dev, &voltage));
	zassert_equal(voltage, 1700000);
	zassert_equal(regulator_set_voltage(dev, 1750000, 1650000), -EINVAL);
	zassert_equal(regulator_set_voltage(dev, 1600001, 1699999), -EINVAL);
	zassert_equal(regulator_list_voltage(dev, UINT_MAX, &voltage), -EINVAL);
	zassert_ok(regulator_get_voltage(dev, &voltage));
	zassert_equal(voltage, 1700000);
}

static void resume_from_isr(const void *arg)
{
	int *result = (int *)arg;

	*result = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
}

ZTEST(vref_pm, test_resume_from_interrupt_context)
{
	int result = -1;

	zassert_ok(regulator_enable(dev));
	zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND));
	irq_offload(resume_from_isr, &result);
	zassert_ok(result);
	zassert_equal(registers_0.CSR & OUTPUT_BITS, OUTPUT_BITS);
	zassert_ok(regulator_disable(dev));
}

ZTEST_SUITE(vref_pm, NULL, NULL, before, NULL, NULL);
