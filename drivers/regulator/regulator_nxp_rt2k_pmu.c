/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_rt2k_pmu_regulator

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <fsl_pmu.h>
#include <fsl_power.h>
#include <fsl_powercon.h>

/*
 * Two tiers of rails share the PMU node:
 *
 * - Full tier (DCDC, LDO0V8, LDO1V8, LDOVDDA): the voltage step is in the PMU, the active mode
 *   in POWERCON PMUCFG_ACTIVE. Every operation reads the hardware, changes its own field and
 *   writes it back, so a value set by the run-mode switch is never rolled back. regulator_mode_t
 *   values are the HAL encodings: kPMU_DcdcModePFM or kPMU_DcdcModePWM for the DCDC,
 *   kPMU_LdoModeHP or kPMU_LdoModeLP for the LDOs.
 *
 * - Reduced tier (LDOA0V8, LDOQ0V8): the HAL only exposes whether they stay on in Sleep and Deep
 *   Sleep. regulator_enable()/regulator_disable() take effect at the next entry into one of these
 *   states, not immediately: they change no hardware, and the SoC power code reads
 *   regulator_is_enabled() while entering the state.
 */

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "The PMU and PMUCFG_ACTIVE are single SoC blocks");

/* VOUT_ADJ 0 selects 0.8 V; every other code is BASE + code x STEP. */
#define REGULATOR_RT2K_PMU_DCDC_DEFAULT_UV   800000
#define REGULATOR_RT2K_PMU_DCDC_BASE_UV      225000
#define REGULATOR_RT2K_PMU_DCDC_STEP_UV      25000
#define REGULATOR_RT2K_PMU_DCDC_FINE_STEP_UV 6250
#define REGULATOR_RT2K_PMU_DCDC_MIN_CODE     1U
#define REGULATOR_RT2K_PMU_DCDC_MIN_UV                                                             \
	(REGULATOR_RT2K_PMU_DCDC_BASE_UV +                                                         \
	 REGULATOR_RT2K_PMU_DCDC_STEP_UV * (int32_t)REGULATOR_RT2K_PMU_DCDC_MIN_CODE)
/* OVL_VTH_ADJ stays at its reset value, which locks the DCDC out above 0.95 V. */
#define REGULATOR_RT2K_PMU_DCDC_MAX_UV       950000

/* PMUCFG_ACTIVE DCDC_MODE encoding */
#define REGULATOR_RT2K_PMU_ACTIVE_DCDC_LOW_LOAD 0U
#define REGULATOR_RT2K_PMU_ACTIVE_DCDC_PWM      1U

/* pmu_ldo_volt_sel_t trims an LDO to its nominal output, +5 %, +10 % or +20 %. */
#define REGULATOR_RT2K_PMU_LDO_0V8_NOMINAL_UV   800000
#define REGULATOR_RT2K_PMU_LDO_1V8_NOMINAL_UV   1800000
#define REGULATOR_RT2K_PMU_LDO_TRIM_P5_PERCENT  5
#define REGULATOR_RT2K_PMU_LDO_TRIM_P10_PERCENT 10
#define REGULATOR_RT2K_PMU_LDO_TRIM_P20_PERCENT 20
#define REGULATOR_RT2K_PMU_PERCENT              100
#define REGULATOR_RT2K_PMU_LDO_STEP_COUNT       (kPMU_LdoVoltSelNomP20 + 1U)

#define REGULATOR_RT2K_PMU_LDO_TRIM_UV(nominal_uv, trim_percent)                                   \
	((nominal_uv) + ((nominal_uv) / REGULATOR_RT2K_PMU_PERCENT) * (trim_percent))

#define REGULATOR_RT2K_PMU_LDO_STEPS_UV(nominal_uv)                                                \
	{                                                                                          \
		[kPMU_LdoVoltSelNom] = (nominal_uv),                                               \
		[kPMU_LdoVoltSelNomP5] = REGULATOR_RT2K_PMU_LDO_TRIM_UV(                           \
			nominal_uv, REGULATOR_RT2K_PMU_LDO_TRIM_P5_PERCENT),                       \
		[kPMU_LdoVoltSelNomP10] = REGULATOR_RT2K_PMU_LDO_TRIM_UV(                          \
			nominal_uv, REGULATOR_RT2K_PMU_LDO_TRIM_P10_PERCENT),                      \
		[kPMU_LdoVoltSelNomP20] = REGULATOR_RT2K_PMU_LDO_TRIM_UV(                          \
			nominal_uv, REGULATOR_RT2K_PMU_LDO_TRIM_P20_PERCENT),                      \
	}

/* PMUCFG_ACTIVE has no ULP code for VDDA_1V8, so all three LDOs take the same modes. */
BUILD_ASSERT(((uint32_t)kPMU_LdoVdda1V8Off == (uint32_t)kPMU_LdoModeOff) &&
		     ((uint32_t)kPMU_LdoVdda1V8HP == (uint32_t)kPMU_LdoModeHP) &&
		     ((uint32_t)kPMU_LdoVdda1V8LP == (uint32_t)kPMU_LdoModeLP),
	     "LDO_VDDA_1V8 mode encoding differs from the other LDOs");

enum regulator_rt2k_pmu_full_kind {
	REGULATOR_RT2K_PMU_FULL_DCDC,
	REGULATOR_RT2K_PMU_FULL_LDO0V8,
	REGULATOR_RT2K_PMU_FULL_LDO1V8,
	REGULATOR_RT2K_PMU_FULL_LDOVDDA,
};

struct regulator_rt2k_pmu_desc {
	enum regulator_rt2k_pmu_full_kind kind;
	/* The members below are used by the LDOs only. */
	uint32_t active_mode_mask;
	const int32_t *ldo_step_uv;
	pmu_ldo_volt_sel_t (*ldo_read_step)(void);
	void (*ldo_write_step)(pmu_ldo_volt_sel_t step);
};

struct regulator_rt2k_pmu_config {
	struct regulator_common_config common;
	/* NULL for the reduced tier */
	const struct regulator_rt2k_pmu_desc *desc;
};

/* No shadow state: every operation reads the current setting back from the hardware. */
struct regulator_rt2k_pmu_data {
	struct regulator_common_data data;
};

/*
 * One lock for every PMU and PMUCFG_ACTIVE access: the LDOs share PMU_LDO_CTRL_REG, each
 * PMU_Config*() read-modify-writes the sensor, IRQ and reset registers shared by all rails, and
 * the SoC run-mode switch writes the DCDC and PMUCFG_ACTIVE from cpu_freq_pstate_set(), which runs
 * in a k_timer callback. k_spin_lock() masks interrupts, so on this single core neither side can
 * interleave with the other. The hardware helpers below expect the caller to hold it.
 */
static struct k_spinlock regulator_rt2k_pmu_lock;

static const int32_t regulator_rt2k_pmu_ldo_0v8_step_uv[REGULATOR_RT2K_PMU_LDO_STEP_COUNT] =
	REGULATOR_RT2K_PMU_LDO_STEPS_UV(REGULATOR_RT2K_PMU_LDO_0V8_NOMINAL_UV);

static const int32_t regulator_rt2k_pmu_ldo_1v8_step_uv[REGULATOR_RT2K_PMU_LDO_STEP_COUNT] =
	REGULATOR_RT2K_PMU_LDO_STEPS_UV(REGULATOR_RT2K_PMU_LDO_1V8_NOMINAL_UV);

static uint32_t regulator_rt2k_pmu_read_active(void)
{
	return POWERCON_GetPmucfgActive(SYSCON__POWERCON_SOC_CTRL);
}

/* PMUCFG_ACTIVE takes effect through a P-Channel update, as in POWER_ApplyRunModePmuState(). */
static void regulator_rt2k_pmu_update_active(uint32_t field_mask, uint32_t field_value)
{
	uint32_t active = regulator_rt2k_pmu_read_active();

	active = (active & ~field_mask) | field_value;
	POWERCON_SetPmucfgActive(SYSCON__POWERCON_SOC_CTRL, active);
	POWERCON_TriggerSocUpdate(SYSCON__POWERCON_SOC_CTRL);
	while (!POWERCON_IsSocUpdateDone(SYSCON__POWERCON_SOC_CTRL)) {
	}
}

static bool regulator_rt2k_pmu_is_dcdc(const struct regulator_rt2k_pmu_desc *desc)
{
	return desc->kind == REGULATOR_RT2K_PMU_FULL_DCDC;
}

/*
 * DCDC (VDD_CORE). The run-mode switch writes the same registers; whichever writes last
 * decides the voltage and the mode.
 */

static int regulator_rt2k_pmu_dcdc_code_for_window(int32_t min_uv, int32_t max_uv, uint8_t *code)
{
	uint32_t steps;
	int32_t lowest_uv;

	if ((min_uv > max_uv) || (min_uv > REGULATOR_RT2K_PMU_DCDC_MAX_UV) ||
	    (max_uv < REGULATOR_RT2K_PMU_DCDC_MIN_UV)) {
		return -EINVAL;
	}

	steps = DIV_ROUND_UP((uint32_t)(MAX(min_uv, REGULATOR_RT2K_PMU_DCDC_MIN_UV) -
					REGULATOR_RT2K_PMU_DCDC_BASE_UV),
			     (uint32_t)REGULATOR_RT2K_PMU_DCDC_STEP_UV);
	lowest_uv = REGULATOR_RT2K_PMU_DCDC_BASE_UV +
		    (int32_t)steps * REGULATOR_RT2K_PMU_DCDC_STEP_UV;
	if (lowest_uv > max_uv) {
		return -EINVAL;
	}

	/* The grid also reaches 0.8 V; use code 0 for it, as the HAL and the reset value do. */
	*code = (lowest_uv == REGULATOR_RT2K_PMU_DCDC_DEFAULT_UV) ? POWER_PMUCFG_CORELEVEL_080V
								   : (uint8_t)steps;

	return 0;
}

static int32_t regulator_rt2k_pmu_dcdc_config_to_uv(const pmu_dcdc_config_t *dcdc)
{
	int32_t coarse_uv = REGULATOR_RT2K_PMU_DCDC_DEFAULT_UV;

	if (dcdc->voutAdj != POWER_PMUCFG_CORELEVEL_080V) {
		coarse_uv = REGULATOR_RT2K_PMU_DCDC_BASE_UV +
			    (int32_t)dcdc->voutAdj * REGULATOR_RT2K_PMU_DCDC_STEP_UV;
	}

	return coarse_uv + (int32_t)dcdc->voutFineAdj * REGULATOR_RT2K_PMU_DCDC_FINE_STEP_UV;
}

static int regulator_rt2k_pmu_dcdc_set_voltage(int32_t min_uv, int32_t max_uv)
{
	uint8_t code;
	int ret;

	ret = regulator_rt2k_pmu_dcdc_code_for_window(min_uv, max_uv, &code);
	if (ret < 0) {
		return ret;
	}

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		pmu_dcdc_config_t dcdc = {0};

		PMU_GetDcdcConfig(SYSCON__PMU, &dcdc);
		dcdc.voutAdj = code;
		dcdc.voutFineAdj = 0U;
		PMU_ConfigDcdc(SYSCON__PMU, &dcdc);

		/* CORE_LVL does not move the rail in active mode, but must mirror VOUT_ADJ. */
		regulator_rt2k_pmu_update_active(POWERCON_SOC_CTRL_PMUCFG_ACTIVE_CORE_LVL_MASK,
						 POWERCON_SOC_CTRL_PMUCFG_ACTIVE_CORE_LVL(code));
	}

	return 0;
}

static int32_t regulator_rt2k_pmu_dcdc_read_uv(void)
{
	pmu_dcdc_config_t dcdc = {0};

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		PMU_GetDcdcConfig(SYSCON__PMU, &dcdc);
	}

	return regulator_rt2k_pmu_dcdc_config_to_uv(&dcdc);
}

/*
 * IPK_ADJ in the PMU sets the over-current limit of the mode and PMUCFG_ACTIVE DCDC_MODE selects
 * the mode; both are written, PMU first, as POWER_ApplyRunModePmuState() does.
 */
static int regulator_rt2k_pmu_dcdc_set_mode(regulator_mode_t mode)
{
	uint32_t active_mode;

	if ((mode != kPMU_DcdcModePFM) && (mode != kPMU_DcdcModePWM)) {
		return -ENOTSUP;
	}

	active_mode = (mode == kPMU_DcdcModePWM) ? REGULATOR_RT2K_PMU_ACTIVE_DCDC_PWM
						 : REGULATOR_RT2K_PMU_ACTIVE_DCDC_LOW_LOAD;

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		pmu_dcdc_config_t dcdc = {0};

		PMU_GetDcdcConfig(SYSCON__PMU, &dcdc);
		dcdc.mode = (pmu_dcdc_mode_t)mode;
		PMU_ConfigDcdc(SYSCON__PMU, &dcdc);

		regulator_rt2k_pmu_update_active(
			POWERCON_SOC_CTRL_PMUCFG_ACTIVE_DCDC_MODE_MASK,
			POWERCON_SOC_CTRL_PMUCFG_ACTIVE_DCDC_MODE(active_mode));
	}

	return 0;
}

static regulator_mode_t regulator_rt2k_pmu_dcdc_read_mode(void)
{
	uint32_t active_mode;

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		active_mode = FIELD_GET(POWERCON_SOC_CTRL_PMUCFG_ACTIVE_DCDC_MODE_MASK,
					regulator_rt2k_pmu_read_active());
	}

	return (active_mode == REGULATOR_RT2K_PMU_ACTIVE_DCDC_PWM) ? kPMU_DcdcModePWM
								   : kPMU_DcdcModePFM;
}

/*
 * LDOs, PMU side. LPVDDREG_*_SEL (lpVoltSel) trims the always-on regulator that supplies the rail
 * in LP mode; it gets the same step as the HP trim, so the requested voltage holds in either mode.
 * VDDA_1V8 has no LP trim. The getters leave mode and sensorEnable, which live in PMUCFG_ACTIVE,
 * untouched, hence the zeroed structures.
 */

static pmu_ldo_volt_sel_t regulator_rt2k_pmu_ldo0v8_read_step(void)
{
	pmu_ldo_vdd0v8_config_t ldo = {0};

	PMU_GetLdoVdd0V8Config(SYSCON__PMU, &ldo);

	return ldo.voltSel;
}

static void regulator_rt2k_pmu_ldo0v8_write_step(pmu_ldo_volt_sel_t step)
{
	pmu_ldo_vdd0v8_config_t ldo = {0};

	PMU_GetLdoVdd0V8Config(SYSCON__PMU, &ldo);
	ldo.voltSel = step;
	ldo.lpVoltSel = step;
	PMU_ConfigLdoVdd0V8(SYSCON__PMU, &ldo);
}

static pmu_ldo_volt_sel_t regulator_rt2k_pmu_ldo1v8_read_step(void)
{
	pmu_ldo_vdd1v8_config_t ldo = {0};

	PMU_GetLdoVdd1V8Config(SYSCON__PMU, &ldo);

	return ldo.voltSel;
}

static void regulator_rt2k_pmu_ldo1v8_write_step(pmu_ldo_volt_sel_t step)
{
	pmu_ldo_vdd1v8_config_t ldo = {0};

	PMU_GetLdoVdd1V8Config(SYSCON__PMU, &ldo);
	ldo.voltSel = step;
	ldo.lpVoltSel = step;
	PMU_ConfigLdoVdd1V8(SYSCON__PMU, &ldo);
}

static pmu_ldo_volt_sel_t regulator_rt2k_pmu_ldovdda_read_step(void)
{
	pmu_ldo_vdda1v8_config_t ldo = {0};

	PMU_GetLdoVdda1V8Config(SYSCON__PMU, &ldo);

	return ldo.voltSel;
}

static void regulator_rt2k_pmu_ldovdda_write_step(pmu_ldo_volt_sel_t step)
{
	pmu_ldo_vdda1v8_config_t ldo = {0};

	PMU_GetLdoVdda1V8Config(SYSCON__PMU, &ldo);
	ldo.voltSel = step;
	PMU_ConfigLdoVdda1V8(SYSCON__PMU, &ldo);
}

/* LDOs, PMUCFG_ACTIVE side: the mode field also switches the LDO off. */

static uint32_t regulator_rt2k_pmu_ldo_read_active_mode(const struct regulator_rt2k_pmu_desc *desc)
{
	return FIELD_GET(desc->active_mode_mask, regulator_rt2k_pmu_read_active());
}

static bool regulator_rt2k_pmu_ldo_is_on(const struct regulator_rt2k_pmu_desc *desc)
{
	return regulator_rt2k_pmu_ldo_read_active_mode(desc) != kPMU_LdoModeOff;
}

static void regulator_rt2k_pmu_ldo_write_active_mode(const struct regulator_rt2k_pmu_desc *desc,
						     uint32_t ldo_mode)
{
	regulator_rt2k_pmu_update_active(desc->active_mode_mask,
					 FIELD_PREP(desc->active_mode_mask, ldo_mode));
}

/* The steps ascend, so the first one inside the window is the lowest. */
static int regulator_rt2k_pmu_ldo_step_for_window(const int32_t *step_uv, int32_t min_uv,
						  int32_t max_uv, pmu_ldo_volt_sel_t *step)
{
	for (uint32_t sel = 0U; sel < REGULATOR_RT2K_PMU_LDO_STEP_COUNT; sel++) {
		if ((step_uv[sel] >= min_uv) && (step_uv[sel] <= max_uv)) {
			*step = (pmu_ldo_volt_sel_t)sel;
			return 0;
		}
	}

	return -EINVAL;
}

static int regulator_rt2k_pmu_ldo_set_voltage(const struct regulator_rt2k_pmu_desc *desc,
					      int32_t min_uv, int32_t max_uv)
{
	pmu_ldo_volt_sel_t step;
	int ret;

	ret = regulator_rt2k_pmu_ldo_step_for_window(desc->ldo_step_uv, min_uv, max_uv, &step);
	if (ret < 0) {
		return ret;
	}

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		desc->ldo_write_step(step);
	}

	return 0;
}

static int32_t regulator_rt2k_pmu_ldo_read_uv(const struct regulator_rt2k_pmu_desc *desc)
{
	pmu_ldo_volt_sel_t step;

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		step = desc->ldo_read_step();
	}

	return desc->ldo_step_uv[step];
}

static int regulator_rt2k_pmu_ldo_set_mode(const struct regulator_rt2k_pmu_desc *desc,
					   regulator_mode_t mode)
{
	int ret = 0;

	if ((mode != kPMU_LdoModeHP) && (mode != kPMU_LdoModeLP)) {
		return -ENOTSUP;
	}

	/* Writing a mode to an LDO that is off would switch it on behind its refcount. */
	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		if (regulator_rt2k_pmu_ldo_is_on(desc)) {
			regulator_rt2k_pmu_ldo_write_active_mode(desc, mode);
		} else {
			ret = -EPERM;
		}
	}

	return ret;
}

static regulator_mode_t regulator_rt2k_pmu_ldo_read_mode(const struct regulator_rt2k_pmu_desc *desc)
{
	uint32_t active_mode;

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		active_mode = regulator_rt2k_pmu_ldo_read_active_mode(desc);
	}

	return (regulator_mode_t)active_mode;
}

/* Full tier API */

static int regulator_rt2k_pmu_enable(const struct device *dev)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	/* The DCDC supplies the core that runs this code, so it is always on. */
	if (regulator_rt2k_pmu_is_dcdc(config->desc)) {
		return 0;
	}

	/* Without stored state an LDO comes back on in HP mode, its reset mode. */
	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		if (!regulator_rt2k_pmu_ldo_is_on(config->desc)) {
			regulator_rt2k_pmu_ldo_write_active_mode(config->desc, kPMU_LdoModeHP);
		}
	}

	return 0;
}

static int regulator_rt2k_pmu_disable(const struct device *dev)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	/* PMUCFG_ACTIVE has no off setting for the DCDC: it supplies the running core. */
	if (regulator_rt2k_pmu_is_dcdc(config->desc)) {
		return -ENOTSUP;
	}

	K_SPINLOCK(&regulator_rt2k_pmu_lock) {
		regulator_rt2k_pmu_ldo_write_active_mode(config->desc, kPMU_LdoModeOff);
	}

	return 0;
}

static int regulator_rt2k_pmu_set_voltage(const struct device *dev, int32_t min_uv,
					  int32_t max_uv)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	if (regulator_rt2k_pmu_is_dcdc(config->desc)) {
		return regulator_rt2k_pmu_dcdc_set_voltage(min_uv, max_uv);
	}

	return regulator_rt2k_pmu_ldo_set_voltage(config->desc, min_uv, max_uv);
}

static int regulator_rt2k_pmu_get_voltage(const struct device *dev, int32_t *volt_uv)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	*volt_uv = regulator_rt2k_pmu_is_dcdc(config->desc)
			   ? regulator_rt2k_pmu_dcdc_read_uv()
			   : regulator_rt2k_pmu_ldo_read_uv(config->desc);

	return 0;
}

static int regulator_rt2k_pmu_set_mode(const struct device *dev, regulator_mode_t mode)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	if (regulator_rt2k_pmu_is_dcdc(config->desc)) {
		return regulator_rt2k_pmu_dcdc_set_mode(mode);
	}

	return regulator_rt2k_pmu_ldo_set_mode(config->desc, mode);
}

static int regulator_rt2k_pmu_get_mode(const struct device *dev, regulator_mode_t *mode)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;

	*mode = regulator_rt2k_pmu_is_dcdc(config->desc)
			? regulator_rt2k_pmu_dcdc_read_mode()
			: regulator_rt2k_pmu_ldo_read_mode(config->desc);

	return 0;
}

static DEVICE_API(regulator, regulator_rt2k_pmu_full_api) = {
	.enable = regulator_rt2k_pmu_enable,
	.disable = regulator_rt2k_pmu_disable,
	.set_voltage = regulator_rt2k_pmu_set_voltage,
	.get_voltage = regulator_rt2k_pmu_get_voltage,
	.set_mode = regulator_rt2k_pmu_set_mode,
	.get_mode = regulator_rt2k_pmu_get_mode,
};

/* A rail that is on at boot starts with a reference held, as regulator_common_init() expects. */
static int regulator_rt2k_pmu_full_init(const struct device *dev)
{
	const struct regulator_rt2k_pmu_config *config = dev->config;
	bool is_enabled = true;

	regulator_common_data_init(dev);

	if (!regulator_rt2k_pmu_is_dcdc(config->desc)) {
		K_SPINLOCK(&regulator_rt2k_pmu_lock) {
			is_enabled = regulator_rt2k_pmu_ldo_is_on(config->desc);
		}
	}

	return regulator_common_init(dev, is_enabled);
}

/*
 * Reduced tier API. enable/disable must not be NULL: regulator_enable() would then return
 * before counting, and the refcount is what the SoC power code reads. They must not block
 * either: regulator_enable() calls them holding the refcount mutex, which regulator_is_enabled()
 * takes with interrupts locked on the way into a low-power state. The regulator API turns a NULL
 * callback into -ENOSYS; these rails report -ENOTSUP instead, since the hardware has no such
 * setting.
 */

static int regulator_rt2k_pmu_standby_request(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int regulator_rt2k_pmu_set_voltage_unsupported(const struct device *dev, int32_t min_uv,
						      int32_t max_uv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(min_uv);
	ARG_UNUSED(max_uv);

	return -ENOTSUP;
}

static int regulator_rt2k_pmu_get_voltage_unsupported(const struct device *dev, int32_t *volt_uv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(volt_uv);

	return -ENOTSUP;
}

static int regulator_rt2k_pmu_set_mode_unsupported(const struct device *dev,
						   regulator_mode_t mode)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(mode);

	return -ENOTSUP;
}

static int regulator_rt2k_pmu_get_mode_unsupported(const struct device *dev,
						   regulator_mode_t *mode)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(mode);

	return -ENOTSUP;
}

static DEVICE_API(regulator, regulator_rt2k_pmu_reduced_api) = {
	.enable = regulator_rt2k_pmu_standby_request,
	.disable = regulator_rt2k_pmu_standby_request,
	.set_voltage = regulator_rt2k_pmu_set_voltage_unsupported,
	.get_voltage = regulator_rt2k_pmu_get_voltage_unsupported,
	.set_mode = regulator_rt2k_pmu_set_mode_unsupported,
	.get_mode = regulator_rt2k_pmu_get_mode_unsupported,
};

/*
 * The refcount starts at zero although the LDO runs: it counts requests to keep the LDO on in
 * low-power states, which is off unless requested.
 */
static int regulator_rt2k_pmu_reduced_init(const struct device *dev)
{
	regulator_common_data_init(dev);

	return regulator_common_init(dev, false);
}

/* The PMU has neither DVS states nor a ship mode. */
static DEVICE_API(regulator_parent, regulator_rt2k_pmu_parent_api) = {};

static const struct regulator_rt2k_pmu_desc regulator_rt2k_pmu_dcdc_desc = {
	.kind = REGULATOR_RT2K_PMU_FULL_DCDC,
};

static const struct regulator_rt2k_pmu_desc regulator_rt2k_pmu_ldo0v8_desc = {
	.kind = REGULATOR_RT2K_PMU_FULL_LDO0V8,
	.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_0V8_MASK,
	.ldo_step_uv = regulator_rt2k_pmu_ldo_0v8_step_uv,
	.ldo_read_step = regulator_rt2k_pmu_ldo0v8_read_step,
	.ldo_write_step = regulator_rt2k_pmu_ldo0v8_write_step,
};

static const struct regulator_rt2k_pmu_desc regulator_rt2k_pmu_ldo1v8_desc = {
	.kind = REGULATOR_RT2K_PMU_FULL_LDO1V8,
	.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDD_1V8_MASK,
	.ldo_step_uv = regulator_rt2k_pmu_ldo_1v8_step_uv,
	.ldo_read_step = regulator_rt2k_pmu_ldo1v8_read_step,
	.ldo_write_step = regulator_rt2k_pmu_ldo1v8_write_step,
};

static const struct regulator_rt2k_pmu_desc regulator_rt2k_pmu_ldovdda_desc = {
	.kind = REGULATOR_RT2K_PMU_FULL_LDOVDDA,
	.active_mode_mask = POWERCON_SOC_CTRL_PMUCFG_ACTIVE_LDO_VDDA_1V8_MASK,
	.ldo_step_uv = regulator_rt2k_pmu_ldo_1v8_step_uv,
	.ldo_read_step = regulator_rt2k_pmu_ldovdda_read_step,
	.ldo_write_step = regulator_rt2k_pmu_ldovdda_write_step,
};

#define REGULATOR_RT2K_PMU_DEFINE(node_id, id, desc_ptr, init_fn, api_ptr)                         \
	static struct regulator_rt2k_pmu_data regulator_rt2k_pmu_data_##id;                        \
                                                                                                   \
	static const struct regulator_rt2k_pmu_config regulator_rt2k_pmu_config_##id = {           \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(node_id),                                \
		.desc = (desc_ptr),                                                                \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_DEFINE(node_id, init_fn, NULL, &regulator_rt2k_pmu_data_##id,                    \
			 &regulator_rt2k_pmu_config_##id, POST_KERNEL,                             \
			 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, api_ptr);

/* Children are matched by node name; the name decides the tier. */
#define REGULATOR_RT2K_PMU_FULL_DEFINE(inst, name)                                                 \
	IF_ENABLED(DT_NODE_HAS_STATUS_OKAY(DT_INST_CHILD(inst, name)),                             \
		   (REGULATOR_RT2K_PMU_DEFINE(DT_INST_CHILD(inst, name), name##inst,               \
					      &regulator_rt2k_pmu_##name##_desc,                   \
					      regulator_rt2k_pmu_full_init,                        \
					      &regulator_rt2k_pmu_full_api)))

#define REGULATOR_RT2K_PMU_REDUCED_DEFINE(inst, name)                                              \
	IF_ENABLED(DT_NODE_HAS_STATUS_OKAY(DT_INST_CHILD(inst, name)),                             \
		   (BUILD_ASSERT((DT_NODE_HAS_PROP(DT_INST_CHILD(inst, name),                      \
						   regulator_min_microvolt) == 0) &&               \
				 (DT_NODE_HAS_PROP(DT_INST_CHILD(inst, name),                      \
						   regulator_max_microvolt) == 0),                 \
				 STRINGIFY(name) " has no voltage setting");                       \
		    REGULATOR_RT2K_PMU_DEFINE(DT_INST_CHILD(inst, name), name##inst, NULL,         \
					      regulator_rt2k_pmu_reduced_init,                     \
					      &regulator_rt2k_pmu_reduced_api)))

#define REGULATOR_RT2K_PMU_DEFINE_ALL(inst)                                                        \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, NULL, POST_KERNEL,                           \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &regulator_rt2k_pmu_parent_api); \
                                                                                                   \
	REGULATOR_RT2K_PMU_FULL_DEFINE(inst, dcdc)                                                 \
	REGULATOR_RT2K_PMU_FULL_DEFINE(inst, ldo0v8)                                               \
	REGULATOR_RT2K_PMU_FULL_DEFINE(inst, ldo1v8)                                               \
	REGULATOR_RT2K_PMU_FULL_DEFINE(inst, ldovdda)                                              \
	REGULATOR_RT2K_PMU_REDUCED_DEFINE(inst, ldoa0v8)                                           \
	REGULATOR_RT2K_PMU_REDUCED_DEFINE(inst, ldoq0v8)

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_RT2K_PMU_DEFINE_ALL)
