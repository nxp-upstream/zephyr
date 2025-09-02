/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include "fsl_cmc.h"
#include "fsl_spc.h"
#include "fsl_vbat.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

/*
 * 1. Set power mode protection
 * 2. Disable low power mode debug
 * 3. Enable Flash Doze mode.
 */
static void set_cmc_configuration(void)
{
	CMC_SetPowerModeProtection(CMC0, kCMC_AllowAllLowPowerModes);
	CMC_LockPowerModeProtectionSetting(CMC0);
	CMC_EnableDebugOperation(CMC0, false);
	CMC_ConfigFlashMode(CMC0, false, false, false);
}

/*
 * Disable Backup SRAM regulator, FRO16K and Bandgap which
 * locates in VBAT power domain for most of power modes.
 *
 */
static void deinit_vbat(void)
{
	VBAT_EnableBackupSRAMRegulator(VBAT0, false);
	VBAT_EnableFRO16k(VBAT0, false);
	while (VBAT_CheckFRO16kEnabled(VBAT0)) {
	};
	VBAT_EnableBandgap(VBAT0, false);
	while (VBAT_CheckBandgapEnabled(VBAT0)) {
	};
}

/* Invoke Low Power/System Off specific Tasks */
__weak void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	/* Set PRIMASK */
	__disable_irq();
	/* Set BASEPRI to 0 */
	irq_unlock(0);

	if (state == PM_STATE_RUNTIME_IDLE) {
		k_cpu_idle();
		return;
	}

	set_cmc_configuration();
	deinit_vbat();

	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		cmc_power_domain_config_t config;

		if (substate_id == 0) {
			/* Set NBU into Sleep Mode */
			RFMC->RF2P4GHZ_CTRL = (RFMC->RF2P4GHZ_CTRL & (~RFMC_RF2P4GHZ_CTRL_LP_MODE_MASK)) |
					       RFMC_RF2P4GHZ_CTRL_LP_MODE(0x1);
			RFMC->RF2P4GHZ_CTRL |= RFMC_RF2P4GHZ_CTRL_LP_ENTER_MASK;

			/* Set MAIN_CORE and MAIN_WAKE power domain into sleep mode. */
			config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
			config.main_domain = kCMC_SleepMode;
			config.wake_domain = kCMC_SleepMode;
			CMC_EnterLowPowerMode(CMC0, &config);
		} else if (substate_id == 1) {
		} else {
			/* Nothing to do */
		}
		break;
	case PM_STATE_STANDBY:
		/* Enable CORE VDD Voltage scaling. */
		SPC_EnableLowPowerModeCoreVDDInternalVoltageScaling(SPC0, true);

		/* Set NBU into Deep Sleep Mode */
		RFMC->RF2P4GHZ_CTRL = (RFMC->RF2P4GHZ_CTRL & (~RFMC_RF2P4GHZ_CTRL_LP_MODE_MASK)) |
				       RFMC_RF2P4GHZ_CTRL_LP_MODE(0x3);
		RFMC->RF2P4GHZ_CTRL |= RFMC_RF2P4GHZ_CTRL_LP_ENTER_MASK;

		/* Set MAIN_CORE and MAIN_WAKE power domain into Deep Sleep Mode. */
		config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		config.main_domain = kCMC_DeepSleepMode;
		config.wake_domain = kCMC_DeepSleepMode;

		CMC_EnterLowPowerMode(CMC0, &config);

		break;
	default:
		LOG_DBG("Unsupported power state %u", state);
		break;
	}
}

/* Handle SOC specific activity after Low Power Mode Exit */
__weak void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	/* Clear PRIMASK */
	__enable_irq();

	if (state == PM_STATE_RUNTIME_IDLE) {
		return;
	}

	if (SPC_CheckPowerDomainLowPowerRequest(SPC0, kSPC_PowerDomain0)) {
		SPC_ClearPowerDomainLowPowerRequestFlag(SPC0, kSPC_PowerDomain0);
	}
	if (SPC_CheckPowerDomainLowPowerRequest(SPC0, kSPC_PowerDomain1)) {
		SPC_ClearPowerDomainLowPowerRequestFlag(SPC0, kSPC_PowerDomain1);
	}
	if (SPC_CheckPowerDomainLowPowerRequest(SPC0, kSPC_PowerDomain2)) {
		RFMC->RF2P4GHZ_CTRL = (RFMC->RF2P4GHZ_CTRL & (~RFMC_RF2P4GHZ_CTRL_LP_MODE_MASK));
		RFMC->RF2P4GHZ_CTRL &= ~RFMC_RF2P4GHZ_CTRL_LP_ENTER_MASK;
		SPC_ClearPowerDomainLowPowerRequestFlag(SPC0, kSPC_PowerDomain2);
	}
	SPC_ClearLowPowerRequest(SPC0);
}
