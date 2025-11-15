/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/pm.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <fsl_cmc.h>
#include <fsl_spc.h>
#include <fsl_wuu.h>
#include <fsl_vbat.h>
#include <soc.h>

#define WUU_WAKEUP_LPTMR0_IDX	6U
#define MCXN_WAKEUP_DELAY	DT_PROP_OR(DT_NODELABEL(spc), wakeup_delay, 0)
#define MCXN_WUU_ADDR		(WUU_Type *)DT_REG_ADDR(DT_INST(0, nxp_wuu))
#define MCXN_CMC_ADDR		(CMC_Type *)DT_REG_ADDR(DT_INST(0, nxp_cmc))
#define MCXN_SPC_ADDR		(SPC_Type *)DT_REG_ADDR(DT_INST(0, nxp_spc))
#define MCXN_VBAT_ADDR		(VBAT_Type *)DT_REG_ADDR(DT_INST(0, nxp_vbat))

void pm_set_wakeup(void)
{
	if(DT_PROP(DT_NODELABEL(lptmr0), clk_source) != 0x1) {
		return;
	}

	VBAT_EnableFRO16k(MCXN_VBAT_ADDR, true);
	VBAT_UngateFRO16k(MCXN_VBAT_ADDR, kVBAT_EnableClockToVddSys);

	WUU_SetInternalWakeUpModulesConfig(MCXN_WUU_ADDR, WUU_WAKEUP_LPTMR0_IDX, kWUU_InternalModuleInterrupt);
}

void pm_enter_hook(void)
{
	SPC_SetExternalVoltageDomainsConfig(MCXN_SPC_ADDR, 0x1EU, 0x0U);
	CMC_SetPowerModeProtection(MCXN_CMC_ADDR, kCMC_AllowAllLowPowerModes);
	CMC_EnableDebugOperation(MCXN_CMC_ADDR, true);
	CMC_ConfigFlashMode(MCXN_CMC_ADDR, true, false);
}

void pm_exit_hook(void)
{
	SPC_ClearPowerDomainLowPowerRequestFlag(MCXN_SPC_ADDR, kSPC_PowerDomain0);
	SPC_ClearPowerDomainLowPowerRequestFlag(MCXN_SPC_ADDR, kSPC_PowerDomain1);
	SPC_ClearLowPowerRequest(MCXN_SPC_ADDR);
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	cmc_power_domain_config_t cmc_config;

	pm_set_wakeup();
	pm_enter_hook();

	__enable_irq();
	__set_BASEPRI(0);
	__ISB();

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		cmc_config.clock_mode  = kCMC_GateCoreClock;
		cmc_config.main_domain = kCMC_ActiveOrSleepMode;
		cmc_config.wake_domain = kCMC_ActiveOrSleepMode;
		__DSB();
		__ISB();
		//printk("Pre sleep sync: SysTick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n",
		//       SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		//printk("Pre sleep: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		//printk("Post sleep: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		break;

	case PM_STATE_SUSPEND_TO_IDLE:
		cmc_config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		cmc_config.main_domain = kCMC_DeepSleepMode;
		cmc_config.wake_domain = kCMC_DeepSleepMode;
		__DSB();
		__ISB();
		//printk("Pre deep sleep sync: SysTick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n",
		      //SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		//printk("Pre deep sleep: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		printk("Post deep sleep: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		break;

	case PM_STATE_STANDBY:
		cmc_config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		cmc_config.main_domain = kCMC_PowerDownMode;
		cmc_config.wake_domain = kCMC_PowerDownMode;
		SPC_SetLowPowerWakeUpDelay(SPC0, MCXN_WAKEUP_DELAY);
		__DSB();
		__ISB();
		//printk("Pre power down sync: SysTick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n",
		//       SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		//printk("Pre power down: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		printk("Post power down: systick CTRL=0x%x LPTMR CSR=0x%x CMR=0x%x CNR=%u\r\n", SysTick->CTRL, LPTMR0->CSR, LPTMR0->CMR, LPTMR0->CNR);
		break;

	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	__enable_irq();
	__ISB();

	pm_exit_hook();
}

void pm_deeppowerpower_exit_hook(void)
{
	if ((CMC_GetSystemResetStatus(MCXN_CMC_ADDR) & kCMC_WakeUpReset) != 0UL) {
		SPC_ClearPeriphIOIsolationFlag(MCXN_SPC_ADDR);
	}
}
