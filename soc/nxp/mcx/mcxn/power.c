/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/pm/pm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <fsl_cmc.h>
#include <fsl_spc.h>
#include <fsl_wuu.h>
#include <soc.h>

LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

#define WUU_WAKEUP_LPTMR0_IDX	6U
#define MCXN_WUU_ADDR		(WUU_Type *)DT_REG_ADDR(DT_INST(0, nxp_wuu))
#define MCXN_CMC_ADDR		(CMC_Type *)DT_REG_ADDR(DT_INST(0, nxp_cmc))
#define MCXN_SPC_ADDR		(SPC_Type *)DT_REG_ADDR(DT_INST(0, nxp_spc))

static void pm_set_wakeup(void)
{
	/* Validate LPTMR0 clock source configuration. */
	if(DT_PROP(DT_NODELABEL(lptmr0), clk_source) != 0x1) {
		LOG_WRN("LPTMR0 clock source is not 16K FRO, cannot be used as wakeup source");
		return;
	}

	WUU_SetInternalWakeUpModulesConfig(MCXN_WUU_ADDR, WUU_WAKEUP_LPTMR0_IDX,
				kWUU_InternalModuleInterrupt);
}

__weak void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	cmc_power_domain_config_t cmc_config = {
		.clock_mode = kCMC_GateAllSystemClocksEnterLowPowerMode,
	};

	/* Enable wakeup source. */
	pm_set_wakeup();

	/* Allow all low power modes, enable debug during low power mode if configured. */
	CMC_SetPowerModeProtection(MCXN_CMC_ADDR, kCMC_AllowAllLowPowerModes);
	CMC_EnableDebugOperation(MCXN_CMC_ADDR, IS_ENABLED(CONFIG_DEBUG));

	/* Set PRIMASK before configuring BASEPRI to prevent interruption before wakeup. */
	__disable_irq();

	/* Set BASEPRI to 0, the interrupt can wakeup the core. */
	__set_BASEPRI(0);
	__ISB();

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		cmc_config.main_domain = kCMC_ActiveOrSleepMode;
		cmc_config.wake_domain = kCMC_ActiveOrSleepMode;
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;

	case PM_STATE_SUSPEND_TO_IDLE:
		cmc_config.main_domain = kCMC_DeepSleepMode;
		cmc_config.wake_domain = kCMC_DeepSleepMode;
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;

	case PM_STATE_STANDBY:
		cmc_config.main_domain = kCMC_PowerDownMode;
		cmc_config.wake_domain = kCMC_PowerDownMode;
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;

	case PM_STATE_SUSPEND_TO_RAM:
		cmc_config.main_domain = kCMC_DeepPowerDown;
		cmc_config.wake_domain = kCMC_DeepPowerDown;
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
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

	__enable_irq();
	__ISB();

	/* Clear low power mode request. */
	SPC_ClearPowerDomainLowPowerRequestFlag(MCXN_SPC_ADDR, kSPC_PowerDomain0);
	SPC_ClearPowerDomainLowPowerRequestFlag(MCXN_SPC_ADDR, kSPC_PowerDomain1);
	SPC_ClearLowPowerRequest(MCXN_SPC_ADDR);
}

void pm_deeppowerpower_exit_post_ops(void)
{
	/* Release the I/O pads and certain peripherals to normal run mode state,
	 * for in deep power down mode they will be in a latched state.
	 */
	if ((CMC_GetSystemResetStatus(MCXN_CMC_ADDR) & kCMC_WakeUpReset) != 0UL) {
		SPC_ClearPeriphIOIsolationFlag(MCXN_SPC_ADDR);
	}
}
