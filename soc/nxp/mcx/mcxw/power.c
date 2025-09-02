/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include "fsl_power.h"
#include "fsl_cmc.h"
#include "fsl_spc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

/* Invoke Low Power/System Off specific Tasks */
__weak void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	/* Set PRIMASK */
	__disable_irq();
	/* Set BASEPRI to 0 */
	irq_unlock(0);

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		cmc_power_domain_config_t config;

		/* Set NBU into Sleep Mode */
		RFMC->RF2P4GHZ_CTRL = (RFMC->RF2P4GHZ_CTRL & (~RFMC_RF2P4GHZ_CTRL_LP_MODE_MASK)) |
				       RFMC_RF2P4GHZ_CTRL_LP_MODE(0x1);
		RFMC->RF2P4GHZ_CTRL |= RFMC_RF2P4GHZ_CTRL_LP_ENTER_MASK;

		/* Set MAIN_CORE and MAIN_WAKE power domain into sleep mode. */
		config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		config.main_domain = kCMC_SleepMode;
		config.wake_domain = kCMC_SleepMode;
		CMC_EnterLowPowerMode(CMC0, &config);

		break;
	case PM_STATE_SUSPEND_TO_IDLE:
		cmc_power_domain_config_t config;

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
	case PM_STATE_STANDBY:

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

	if (state == PM_STATE_STANDBY) {

	}
	/* Clear PRIMASK */
	__enable_irq();
}

void nxp_rw6xx_power_init(void)
{
}
