/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/cpu_freq/cpu_freq.h>
#include <zephyr/cpu_freq/pstate.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fsl_power.h>
#include <fsl_powercon.h>

#include "stay_awake.h"

/* VDD_CORE the HAL programs into the DCDC for each run mode */
#define HP_RUN_VDD_CORE_UV     900000
#define NORMAL_RUN_VDD_CORE_UV 800000

static const struct pstate *const hp_run = PSTATE_DT_GET(DT_NODELABEL(pstate_od_run));
static const struct pstate *const normal_run = PSTATE_DT_GET(DT_NODELABEL(pstate_normal_run));

static const struct device *const dcdc = DEVICE_DT_GET(DT_NODELABEL(dcdc));
static const struct device *const ldo1v8 = DEVICE_DT_GET(DT_NODELABEL(ldo1v8));

static void enter_run_mode(const struct pstate *run_mode, const char *name)
{
	zassert_ok(cpu_freq_pstate_set(run_mode), "Cannot enter %s", name);
}

static uint32_t active_core_level(void)
{
	return FIELD_GET(POWERCON_SOC_CTRL_PMUCFG_ACTIVE_CORE_LVL_MASK,
			 POWERCON_GetPmucfgActive(SYSCON__POWERCON_SOC_CTRL));
}

static void assert_dcdc_voltage(int32_t expected_uv, const char *run_mode_name)
{
	int32_t volt_uv;

	zassert_ok(regulator_get_voltage(dcdc, &volt_uv), "dcdc has no voltage");
	zassert_equal(volt_uv, expected_uv, "dcdc reads %d uV in %s, expected %d uV", volt_uv,
		      run_mode_name, expected_uv);
}

ZTEST(run_modes, test_ldo1v8_mode_write_keeps_the_hp_run_core_level)
{
	regulator_mode_t mode;

	enter_run_mode(hp_run, "HP Run");
	zassert_equal(active_core_level(), POWER_PMUCFG_CORELEVEL_090V,
		      "HP Run left CORE_LVL at 0x%02x", active_core_level());

	/* The mode the rail already has: the write goes through without changing the rail. */
	zassert_ok(regulator_get_mode(ldo1v8, &mode), "ldo1v8 has no mode");
	zassert_ok(regulator_set_mode(ldo1v8, mode), "ldo1v8 rejects its own mode");

	zassert_equal(active_core_level(), POWER_PMUCFG_CORELEVEL_090V,
		      "The ldo1v8 mode write moved CORE_LVL to 0x%02x", active_core_level());
}

ZTEST(run_modes, test_dcdc_voltage_follows_the_run_mode)
{
	enter_run_mode(hp_run, "HP Run");
	assert_dcdc_voltage(HP_RUN_VDD_CORE_UV, "HP Run");

	enter_run_mode(normal_run, "Normal Run");
	assert_dcdc_voltage(NORMAL_RUN_VDD_CORE_UV, "Normal Run");

	enter_run_mode(hp_run, "HP Run");
	assert_dcdc_voltage(HP_RUN_VDD_CORE_UV, "HP Run");
}

/* soc_clock_init() boots into HP Run. */
static void run_modes_after(void *fixture)
{
	ARG_UNUSED(fixture);

	enter_run_mode(hp_run, "HP Run");
}

ZTEST_SUITE(run_modes, NULL, stay_awake_setup, NULL, run_modes_after, stay_awake_teardown);
