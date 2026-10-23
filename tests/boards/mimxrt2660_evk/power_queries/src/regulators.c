/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fsl_pmu.h>
#include <fsl_power.h>
#include <fsl_powercon.h>

#include "stay_awake.h"

/*
 * Every value written to a rail is the one it already has or its nominal setting,
 * and the settings found before a test are written back after it: the tests run on
 * the board they supply. No test switches an LDO off.
 */

/* Outputs at the kPMU_LdoVoltSelNom trim */
#define LDO_0V8_NOMINAL_UV 800000
#define LDO_1V8_NOMINAL_UV 1800000

/* The LDO mode out of reset */
#define LDO_NOMINAL_MODE kPMU_LdoModeHP

/* LDOA_0V8 and LDOQ_0V8 are 0.8 V supplies; they have no voltage setting to request. */
#define REDUCED_TIER_REQUEST_UV LDO_0V8_NOMINAL_UV

static const struct device *const dcdc = DEVICE_DT_GET(DT_NODELABEL(dcdc));
static const struct device *const ldo1v8 = DEVICE_DT_GET(DT_NODELABEL(ldo1v8));

static const struct device *const reduced_tier_rails[] = {
	DEVICE_DT_GET(DT_NODELABEL(ldoa0v8)),
	DEVICE_DT_GET(DT_NODELABEL(ldoq0v8)),
};

struct full_tier_ldo {
	const struct device *dev;
	int32_t nominal_uv;
	/* PMUCFG_ACTIVE field holding the mode */
	uint32_t active_mode_mask;
	/* PMU_LDO_CTRL_REG trims of the HP and the LP regulator; VDDA_1V8 has no LP trim. */
	uint32_t hp_trim_mask;
	uint32_t lp_trim_mask;
};

static const struct full_tier_ldo full_tier_ldos[] = {
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(ldo0v8)),
		.nominal_uv = LDO_0V8_NOMINAL_UV,
		.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_0V8_MASK,
		.hp_trim_mask = PMU_PMU_LDO_CTRL_REG_PMU_VDD_0V8_SEL_MASK,
		.lp_trim_mask = PMU_PMU_LDO_CTRL_REG_PMU_LPVDDREG_0V8_SEL_MASK,
	},
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(ldo1v8)),
		.nominal_uv = LDO_1V8_NOMINAL_UV,
		.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_1V8_MASK,
		.hp_trim_mask = PMU_PMU_LDO_CTRL_REG_PMU_VDD_1V8_SEL_MASK,
		.lp_trim_mask = PMU_PMU_LDO_CTRL_REG_PMU_LPVDDREG_1V8_SEL_MASK,
	},
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(ldovdda)),
		.nominal_uv = LDO_1V8_NOMINAL_UV,
		.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDDA_1V8_MASK,
		.hp_trim_mask = PMU_PMU_LDO_CTRL_REG_PMU_VDDA_1V8_SEL_MASK,
		.lp_trim_mask = 0U,
	},
};

/* The registers the regulator driver shares between the rails */
struct pmu_registers {
	uint32_t ldo_ctrl;
	uint32_t sensor_ctrl0;
	uint32_t sensor_ctrl1;
	uint32_t irq_enable;
	uint32_t warm_reset;
	uint32_t cold_reset;
	uint32_t pmucfg_active;
};

/* The LDO settings found before a test */
struct ldo_settings {
	pmu_ldo_vdd0v8_config_t ldo0v8;
	pmu_ldo_vdd1v8_config_t ldo1v8;
	pmu_ldo_vdda1v8_config_t ldovdda;
	regulator_mode_t ldo1v8_mode;
};

static struct ldo_settings saved_settings;

static uint32_t pmucfg_active_read(void)
{
	return POWERCON_GetPmucfgActive(SYSCON__POWERCON_SOC_CTRL);
}

static void pmu_registers_read(struct pmu_registers *regs)
{
	regs->ldo_ctrl = SYSCON__PMU->PMU_LDO_CTRL_REG;
	regs->sensor_ctrl0 = SYSCON__PMU->PMU_SENSOR_CTRL0_REG;
	regs->sensor_ctrl1 = SYSCON__PMU->PMU_SENSOR_CTRL1_REG;
	regs->irq_enable = SYSCON__PMU->PMU_DIG_IRQ_EN_REG;
	regs->warm_reset = SYSCON__PMU->PMU_RESETCON_WARM_CTRL;
	regs->cold_reset = SYSCON__PMU->PMU_RESETCON_COLD_CTRL;
	regs->pmucfg_active = pmucfg_active_read();
}

/* The bits that differ between two reads */
static struct pmu_registers pmu_registers_diff(const struct pmu_registers *before,
					       const struct pmu_registers *after)
{
	return (struct pmu_registers){
		.ldo_ctrl = before->ldo_ctrl ^ after->ldo_ctrl,
		.sensor_ctrl0 = before->sensor_ctrl0 ^ after->sensor_ctrl0,
		.sensor_ctrl1 = before->sensor_ctrl1 ^ after->sensor_ctrl1,
		.irq_enable = before->irq_enable ^ after->irq_enable,
		.warm_reset = before->warm_reset ^ after->warm_reset,
		.cold_reset = before->cold_reset ^ after->cold_reset,
		.pmucfg_active = before->pmucfg_active ^ after->pmucfg_active,
	};
}

static void assert_only_allowed_bits_changed(const char *operation,
					     const struct pmu_registers *changed,
					     const struct pmu_registers *allowed)
{
	zassert_equal(changed->ldo_ctrl & ~allowed->ldo_ctrl, 0U,
		      "%s changed PMU_LDO_CTRL_REG bits 0x%08x", operation,
		      changed->ldo_ctrl & ~allowed->ldo_ctrl);
	zassert_equal(changed->sensor_ctrl0 & ~allowed->sensor_ctrl0, 0U,
		      "%s changed PMU_SENSOR_CTRL0_REG bits 0x%08x", operation,
		      changed->sensor_ctrl0 & ~allowed->sensor_ctrl0);
	zassert_equal(changed->sensor_ctrl1 & ~allowed->sensor_ctrl1, 0U,
		      "%s changed PMU_SENSOR_CTRL1_REG bits 0x%08x", operation,
		      changed->sensor_ctrl1 & ~allowed->sensor_ctrl1);
	zassert_equal(changed->irq_enable & ~allowed->irq_enable, 0U,
		      "%s changed PMU_DIG_IRQ_EN_REG bits 0x%08x", operation,
		      changed->irq_enable & ~allowed->irq_enable);
	zassert_equal(changed->warm_reset & ~allowed->warm_reset, 0U,
		      "%s changed PMU_RESETCON_WARM_CTRL bits 0x%08x", operation,
		      changed->warm_reset & ~allowed->warm_reset);
	zassert_equal(changed->cold_reset & ~allowed->cold_reset, 0U,
		      "%s changed PMU_RESETCON_COLD_CTRL bits 0x%08x", operation,
		      changed->cold_reset & ~allowed->cold_reset);
	zassert_equal(changed->pmucfg_active & ~allowed->pmucfg_active, 0U,
		      "%s changed PMUCFG_ACTIVE bits 0x%08x", operation,
		      changed->pmucfg_active & ~allowed->pmucfg_active);
}

static bool ldo_is_on_in_hardware(const struct full_tier_ldo *ldo)
{
	return FIELD_GET(ldo->active_mode_mask, pmucfg_active_read()) != kPMU_LdoModeOff;
}

static void assert_trim_is_nominal(const struct full_tier_ldo *ldo, uint32_t trim_mask,
				   uint32_t ldo_ctrl)
{
	zassert_equal(FIELD_GET(trim_mask, ldo_ctrl), kPMU_LdoVoltSelNom,
		      "%s: trim field 0x%08x of PMU_LDO_CTRL_REG is not nominal", ldo->dev->name,
		      trim_mask);
}

static void assert_nominal_voltage_write_is_isolated(const struct full_tier_ldo *ldo)
{
	const struct pmu_registers allowed = {
		.ldo_ctrl = ldo->hp_trim_mask | ldo->lp_trim_mask,
	};
	struct pmu_registers before;
	struct pmu_registers after;
	struct pmu_registers changed;
	int32_t volt_uv;

	pmu_registers_read(&before);
	zassert_ok(regulator_set_voltage(ldo->dev, ldo->nominal_uv, ldo->nominal_uv),
		   "%s rejects its nominal %d uV", ldo->dev->name, ldo->nominal_uv);
	pmu_registers_read(&after);

	assert_trim_is_nominal(ldo, ldo->hp_trim_mask, after.ldo_ctrl);
	if (ldo->lp_trim_mask != 0U) {
		assert_trim_is_nominal(ldo, ldo->lp_trim_mask, after.ldo_ctrl);
	}

	zassert_ok(regulator_get_voltage(ldo->dev, &volt_uv), "%s has no voltage", ldo->dev->name);
	zassert_equal(volt_uv, ldo->nominal_uv, "%s reads %d uV after %d uV was set",
		      ldo->dev->name, volt_uv, ldo->nominal_uv);

	changed = pmu_registers_diff(&before, &after);
	assert_only_allowed_bits_changed(ldo->dev->name, &changed, &allowed);
}

static void assert_enable_is_only_counted(const struct device *rail)
{
	const struct pmu_registers no_change = {0};
	struct pmu_registers before;
	struct pmu_registers after;
	struct pmu_registers changed;

	zassert_false(regulator_is_enabled(rail), "%s is requested before the test", rail->name);

	pmu_registers_read(&before);
	zassert_ok(regulator_enable(rail), "Cannot enable %s", rail->name);
	zassert_true(regulator_is_enabled(rail), "%s does not count its enable", rail->name);
	zassert_ok(regulator_disable(rail), "Cannot disable %s", rail->name);
	zassert_false(regulator_is_enabled(rail), "%s does not count its disable", rail->name);
	pmu_registers_read(&after);

	changed = pmu_registers_diff(&before, &after);
	assert_only_allowed_bits_changed(rail->name, &changed, &no_change);
}

static void assert_voltage_and_mode_unsupported(const struct device *rail)
{
	const struct pmu_registers no_change = {0};
	struct pmu_registers before;
	struct pmu_registers after;
	struct pmu_registers changed;
	int32_t volt_uv;
	regulator_mode_t mode;

	pmu_registers_read(&before);
	zassert_equal(regulator_set_voltage(rail, REDUCED_TIER_REQUEST_UV, REDUCED_TIER_REQUEST_UV),
		      -ENOTSUP, "%s accepts a voltage", rail->name);
	zassert_equal(regulator_get_voltage(rail, &volt_uv), -ENOTSUP, "%s reports a voltage",
		      rail->name);
	zassert_equal(regulator_set_mode(rail, LDO_NOMINAL_MODE), -ENOTSUP, "%s accepts a mode",
		      rail->name);
	zassert_equal(regulator_get_mode(rail, &mode), -ENOTSUP, "%s reports a mode", rail->name);
	pmu_registers_read(&after);

	changed = pmu_registers_diff(&before, &after);
	assert_only_allowed_bits_changed(rail->name, &changed, &no_change);
}

/*
 * Writes back the configuration the getter reads, then reads it again, so the
 * rail keeps its setting. Both copies start zeroed: the fields the PMU does not
 * hold, and the padding, compare equal.
 */
#define ASSERT_PMU_CONFIG_ROUND_TRIP(config_type, get_config, set_config)                          \
	do {                                                                                       \
		config_type written = {0};                                                         \
		config_type read_back = {0};                                                       \
                                                                                                   \
		get_config(SYSCON__PMU, &written);                                                 \
		set_config(SYSCON__PMU, &written);                                                 \
		get_config(SYSCON__PMU, &read_back);                                               \
                                                                                                   \
		zassert_mem_equal(&read_back, &written, sizeof(written),                           \
				  "%s() differs from what %s() wrote",                             \
				  STRINGIFY(get_config), STRINGIFY(set_config));                   \
	} while (false)

ZTEST(regulators, test_full_tier_reports_the_hardware_state)
{
	zassert_true(regulator_is_enabled(dcdc), "dcdc supplies the core but reads as disabled");

	ARRAY_FOR_EACH_PTR(full_tier_ldos, ldo) {
		zassert_equal(regulator_is_enabled(ldo->dev), ldo_is_on_in_hardware(ldo),
			      "%s refcount disagrees with PMUCFG_ACTIVE", ldo->dev->name);
	}
}

ZTEST(regulators, test_hal_getters_read_back_the_config_write)
{
	ASSERT_PMU_CONFIG_ROUND_TRIP(pmu_dcdc_config_t, PMU_GetDcdcConfig, PMU_ConfigDcdc);
	ASSERT_PMU_CONFIG_ROUND_TRIP(pmu_ldo_vdd0v8_config_t, PMU_GetLdoVdd0V8Config,
				     PMU_ConfigLdoVdd0V8);
	ASSERT_PMU_CONFIG_ROUND_TRIP(pmu_ldo_vdd1v8_config_t, PMU_GetLdoVdd1V8Config,
				     PMU_ConfigLdoVdd1V8);
	ASSERT_PMU_CONFIG_ROUND_TRIP(pmu_ldo_vdda1v8_config_t, PMU_GetLdoVdda1V8Config,
				     PMU_ConfigLdoVdda1V8);
}

ZTEST(regulators, test_ldo_voltage_write_keeps_the_other_fields)
{
	ARRAY_FOR_EACH_PTR(full_tier_ldos, ldo) {
		assert_nominal_voltage_write_is_isolated(ldo);
	}
}

ZTEST(regulators, test_ldo1v8_mode_write_changes_only_its_field)
{
	const struct pmu_registers allowed = {
		.pmucfg_active = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_1V8_MASK,
	};
	struct pmu_registers before;
	struct pmu_registers after;
	struct pmu_registers changed;

	pmu_registers_read(&before);
	zassert_ok(regulator_set_mode(ldo1v8, LDO_NOMINAL_MODE), "ldo1v8 rejects its nominal mode");
	pmu_registers_read(&after);

	zassert_equal(
		FIELD_GET(POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_1V8_MASK, after.pmucfg_active),
		LDO_NOMINAL_MODE, "PMUCFG_ACTIVE does not hold the ldo1v8 mode written");

	changed = pmu_registers_diff(&before, &after);
	assert_only_allowed_bits_changed(ldo1v8->name, &changed, &allowed);
}

ZTEST(regulators, test_reduced_tier_enable_is_counted)
{
	ARRAY_FOR_EACH(reduced_tier_rails, i) {
		assert_enable_is_only_counted(reduced_tier_rails[i]);
	}
}

ZTEST(regulators, test_reduced_tier_has_no_voltage_or_mode)
{
	ARRAY_FOR_EACH(reduced_tier_rails, i) {
		assert_voltage_and_mode_unsupported(reduced_tier_rails[i]);
	}
}

static void regulators_before(void *fixture)
{
	ARG_UNUSED(fixture);

	PMU_GetLdoVdd0V8Config(SYSCON__PMU, &saved_settings.ldo0v8);
	PMU_GetLdoVdd1V8Config(SYSCON__PMU, &saved_settings.ldo1v8);
	PMU_GetLdoVdda1V8Config(SYSCON__PMU, &saved_settings.ldovdda);
	zassert_ok(regulator_get_mode(ldo1v8, &saved_settings.ldo1v8_mode), "ldo1v8 has no mode");
}

/*
 * The trims go back through the HAL: regulator_set_voltage() gives the LP trim
 * the HP one, which would not restore an LP trim of its own.
 */
static void regulators_after(void *fixture)
{
	ARG_UNUSED(fixture);

	PMU_ConfigLdoVdd0V8(SYSCON__PMU, &saved_settings.ldo0v8);
	PMU_ConfigLdoVdd1V8(SYSCON__PMU, &saved_settings.ldo1v8);
	PMU_ConfigLdoVdda1V8(SYSCON__PMU, &saved_settings.ldovdda);
	zassert_ok(regulator_set_mode(ldo1v8, saved_settings.ldo1v8_mode),
		   "Cannot restore the ldo1v8 mode");

	ARRAY_FOR_EACH(reduced_tier_rails, i) {
		if (regulator_is_enabled(reduced_tier_rails[i])) {
			zassert_ok(regulator_disable(reduced_tier_rails[i]), "Cannot release %s",
				   reduced_tier_rails[i]->name);
		}
	}
}

ZTEST_SUITE(regulators, NULL, stay_awake_setup, regulators_before, regulators_after,
	    stay_awake_teardown);
