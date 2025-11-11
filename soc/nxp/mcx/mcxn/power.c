/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/pm/pm.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <fsl_cmc.h>
#include <fsl_spc.h>
#include <fsl_wuu.h>
#include <soc.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

#define WUU_WAKEUP_LPTMR0_IDX	6U
#define WUU_M_LPTMR0_DMA_IDX	4U	/* WUU_M4DR: LPTMR0 DMA */
#define WUU_M_LPTMR0_TRG_IDX	6U	/* WUU_M6DR: LPTMR0 Trigger */
#define MCXN_WUU_ADDR		(WUU_Type *)DT_REG_ADDR(DT_INST(0, nxp_wuu))
#define MCXN_CMC_ADDR		(CMC_Type *)DT_REG_ADDR(DT_INST(0, nxp_cmc))
#define MCXN_SPC_ADDR		(SPC_Type *)DT_REG_ADDR(DT_INST(0, nxp_spc))

/*
 * Ensure LPTMR0 does not carry a stale compare flag (TCF) into standby.
 * If TCF is set before WFI, WUU may immediately request a temporary wake
 * (wake=0x4) even when the real compare event hasn't been serviced yet.
 * Try to clear TCF, and if it re-asserts immediately with TEN set, briefly
 * gate TEN to force a clean start without altering CMR.
 */
//static void lptmr0_scrub_pre_standby(void)
//{
//	if (!DT_NODE_HAS_STATUS(DT_NODELABEL(lptmr0), okay)) {
//		return;
//	}
//
//	LPTMR_Type *lptmr0 = (LPTMR_Type *)DT_REG_ADDR(DT_NODELABEL(lptmr0));
//	uint32_t csr = lptmr0->CSR;
//	uint32_t cmr = lptmr0->CMR;
//	uint32_t psr = lptmr0->PSR;
//
//	/* TCF set before entry is a strong indicator of intermediate wake loops */
//	bool ten_set = (csr & LPTMR_CSR_TEN_MASK) != 0U;
//	bool tcf_set = (csr & LPTMR_CSR_TCF_MASK) != 0U;
//	if (tcf_set) {
//		printk("[LPTMR scrub] pre: CSR=%#x CMR=%u PSR=%u (TEN=%u TCF=%u)\r\n",
//			   csr, cmr, psr, (unsigned)ten_set, (unsigned)tcf_set);
//
//		/* Attempt to clear TCF while keeping configuration */
//		LPTMR_ClearStatusFlags(lptmr0, kLPTMR_TimerCompareFlag);
//		__DSB();
//		__ISB();
//		uint32_t csr_after = lptmr0->CSR;
//		bool tcf_after = (csr_after & LPTMR_CSR_TCF_MASK) != 0U;
//
//		if (tcf_after && ten_set) {
//			/* If TCF immediately re-asserts due to an in-progress match, toggle TEN */
//			lptmr0->CSR &= ~LPTMR_CSR_TEN_MASK;
//			__DSB();
//			__ISB();
//			LPTMR_ClearStatusFlags(lptmr0, kLPTMR_TimerCompareFlag);
//			__DSB();
//			__ISB();
//			/* Restore TEN without changing compare configuration */
//			lptmr0->CSR |= LPTMR_CSR_TEN_MASK;
//			__DSB();
//			__ISB();
//			csr_after = lptmr0->CSR;
//			tcf_after = (csr_after & LPTMR_CSR_TCF_MASK) != 0U;
//		}
//
//		printk("[LPTMR scrub] post: CSR=%#x (TEN=%u TCF=%u)\r\n",
//			   csr_after, (unsigned)((csr_after & LPTMR_CSR_TEN_MASK)!=0U), (unsigned)tcf_after);
//	}
//}

/* Lightweight dump to identify which IRQ is pending around WFI */
//static void dump_core_irq_state(const char *tag)
//{
//	uint32_t primask = __get_PRIMASK();
//	uint32_t basepri = __get_BASEPRI();
//	uint32_t scr = SCB->SCR;
//	uint32_t icsr = SCB->ICSR;
//	uint32_t shcsr = SCB->SHCSR;
//
//	uint32_t vectpending = (icsr >> SCB_ICSR_VECTPENDING_Pos) & 0x1FFU;
//	uint32_t pendst = (icsr & SCB_ICSR_PENDSTSET_Msk) ? 1U : 0U;
//	uint32_t pendsv = (icsr & SCB_ICSR_PENDSVSET_Msk) ? 1U : 0U;
//
//	printk("[SOC %s] PRIMASK=%#x BASEPRI=%#x SCR=%#x ICSR=%#x (VECTPENDING=%u PENDST=%u PENDSV=%u) SHCSR=%#x\r\n",
//		   tag, primask, basepri, scr, icsr, vectpending, pendst, pendsv, shcsr);
//
//	/* SysTick quick state */
//	printk("[SOC %s] SysTick: CTRL=%#x LOAD=%u VAL=%u\r\n", tag, SysTick->CTRL, SysTick->LOAD, SysTick->VAL);
//
//	/* NVIC pending/enabled sets: number of banks = INTLINESNUM+1 (from ICTR) */
//	uint32_t banks = ((SCnSCB->ICTR & 0xFU) + 1U);
//	for (uint32_t i = 0; i < banks; i++) {
//		uint32_t ispr = NVIC->ISPR[i];
//		uint32_t iser = NVIC->ISER[i];
//		if (ispr != 0U) {
//			printk("[SOC %s] NVIC ISPR[%u]=%#010x (EN=%#010x)\r\n", tag, i, ispr, iser);
//		}
//	}
//}

/*
 * Minimal handler for OR (IRQ0) aggregator: we don't use it as a wake source,
 * but when it becomes pending it causes WFI to abort immediately if left unhandled.
 * Install a trivial ISR to consume/clear the pending once when enabled.
 */
//static void or_irq_isr(const void *arg)
//{
//	ARG_UNUSED(arg);
//	/* Clear NVIC pending and immediately mask to avoid storms if level-asserted */
//	NVIC_ClearPendingIRQ(OR_IRQn);
//	irq_disable(OR_IRQn);
//}
//
//static int or_irq_setup(const struct device *dev)
//{
//	ARG_UNUSED(dev);
//	/* Connect ISR but keep it disabled by default; we'll enable around standby entry. */
//	IRQ_CONNECT(OR_IRQn, 0, or_irq_isr, NULL, 0);
//	irq_disable(OR_IRQn);
//	NVIC_ClearPendingIRQ(OR_IRQn);
//	return 0;
//}

//SYS_INIT(or_irq_setup, PRE_KERNEL_2, 0);

/* Track intermediate WUU wake loops before final LPTMR compare fires */
//static uint32_t wuu_intermediate_wakes;
//static uint32_t standby_entries;

void pm_set_wakeup(void)
{
	/* Validate LPTMR0 clock source configuration. */
	if(DT_PROP(DT_NODELABEL(lptmr0), clk_source) != 0x1) {
		LOG_WRN("LPTMR0 clock source is not 16K FRO, cannot be used as wakeup source");
		return;
	}
//	/* Ensure only LPTMR is used as WUU wake source: clear other modules and any external flags */
//#if defined(FSL_FEATURE_WUU_HAS_MF) && FSL_FEATURE_WUU_HAS_MF
//	uint32_t mf = WUU_GetModuleInterruptFlag(MCXN_WUU_ADDR);
//	if (mf) {
//		printk("WUU MF (pre-config)=%#x\r\n", mf);
//		/* Clear any other module wakeup configuration reported in MF */
//		for (uint8_t idx = 0; idx < 32U; idx++) {
//			if ((mf & (1UL << idx)) && (idx != WUU_WAKEUP_LPTMR0_IDX)) {
//				WUU_ClearInternalWakeUpModulesConfig(MCXN_WUU_ADDR, idx, kWUU_InternalModuleInterrupt);
//			}
//		}
//	}
//#endif
//	/* Print and sanitize WUU ME/DE: disable LPTMR0 DMA/Trigger to avoid temporary wakes */
//	uint32_t me = WUU0->ME;
//	uint32_t de = WUU0->DE;
//	printk("WUU ME=%#x DE=%#x (pre)\r\n", me, de);
//	de &= ~((1UL << WUU_M_LPTMR0_DMA_IDX) | (1UL << WUU_M_LPTMR0_TRG_IDX));
//	WUU0->DE = de;
//	printk("WUU DE=%#x (post clr LPTMR0 DMA/TRG)\r\n", WUU0->DE);
//	/* Clear all external wake flags to avoid stale wake conditions */
//	WUU_ClearExternalWakeUpPinsFlag(MCXN_WUU_ADDR, 0xFFFFFFFFu);

	/* Configure LPTMR as the only internal wake source */
	WUU_SetInternalWakeUpModulesConfig(MCXN_WUU_ADDR, WUU_WAKEUP_LPTMR0_IDX, kWUU_InternalModuleInterrupt);
       //printk("Wakeup source setup success\r\n");
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	cmc_power_domain_config_t cmc_config;

	/* Enable wakeup source. */
	pm_set_wakeup();

	/* Allow all low power modes, enable debug during low power mode if configured. */
	CMC_SetPowerModeProtection(MCXN_CMC_ADDR, kCMC_AllowAllLowPowerModes);
	CMC_EnableDebugOperation(MCXN_CMC_ADDR, false);
	CMC_ConfigFlashMode(MCXN_CMC_ADDR, true, false);

	/* Isolate some power domains that are not used in low power modes.*/
	//SPC_SetExternalVoltageDomainsConfig(APP_SPC, APP_SPC_LPTMR_LPISO_VALUE, 0x0U);

		irq_disable(OR_IRQn);
		NVIC_ClearPendingIRQ(OR_IRQn);
		irq_disable(LP_FLEXCOMM4_IRQn);
		NVIC_ClearPendingIRQ(LP_FLEXCOMM4_IRQn);
               
	/* Ensure interrupts are allowed during WFI so that only true wake sources resume execution. */
	__set_BASEPRI(0);
	__enable_irq();
	__ISB();
        
	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		cmc_config.clock_mode  = kCMC_GateCoreClock;
		cmc_config.main_domain = kCMC_ActiveOrSleepMode;
		cmc_config.wake_domain = kCMC_ActiveOrSleepMode;
		printk("ready to enter sleep mode.\r\n");
		/* Debug: snapshot core/NVIC/SysTick state before WFI 
		{
			uint32_t icsr = SCB->ICSR;
			printk("pre-WFI(runtime): ICSR=%#x SYST_CTR=%#x LOAD=%u VAL=%u\r\n",
			       icsr, SysTick->CTRL, SysTick->LOAD, SysTick->VAL);
		}*/
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;

	case PM_STATE_SUSPEND_TO_IDLE:
		cmc_config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		cmc_config.main_domain = kCMC_DeepSleepMode;
		cmc_config.wake_domain = kCMC_DeepSleepMode;
		printk("ready to enter deep sleep mode.\r\n");
		/* Debug: snapshot core/NVIC/SysTick state before WFI 
		{
			uint32_t icsr = SCB->ICSR;
			printk("pre-WFI(deepsleep): ICSR=%#x SYST_CTR=%#x LOAD=%u VAL=%u\r\n",
			       icsr, SysTick->CTRL, SysTick->LOAD, SysTick->VAL);
		}*/
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;

	case PM_STATE_STANDBY:
		cmc_config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
		cmc_config.main_domain = kCMC_PowerDownMode;
		cmc_config.wake_domain = kCMC_PowerDownMode;
		/* Pre-WFI diagnostics & cleanup of known noisy sources */
//		dump_core_irq_state("pre-standby");
//		/* Log WUU ME/DE to ensure only expected sources are enabled */
//		printk("WUU ME=%#x DE=%#x (pre-standby)\r\n", WUU0->ME, WUU0->DE);
//		/* Optional: snapshot LPTMR registers pre-entry */
//		if (DT_NODE_HAS_STATUS(DT_NODELABEL(lptmr0), okay)) {
//			LPTMR_Type *lptmr0 = (LPTMR_Type *)DT_REG_ADDR(DT_NODELABEL(lptmr0));
//			uint32_t csr0 = lptmr0->CSR;
//			uint32_t cmr0 = lptmr0->CMR;
//			uint32_t psr0 = lptmr0->PSR;
//			printk("LPTMR0 pre-standby CSR=%u CMR=%u PSR=%u\r\n", csr0, cmr0, psr0);
//		}
//		/* Clear a possible always-pending aggregator, then temporarily enable to service once if pending. */
//		NVIC_ClearPendingIRQ(OR_IRQn);
//		irq_enable(OR_IRQn);
//		SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
//		__DSB();
//		__ISB();
//		dump_core_irq_state("pre-standby-after-clear");
		printk("ready to enter power down mode.\r\n");
//		/* Optional: clear external WUU pin flags if any latched (OR isn't from WUU, but harmless) */
//		if (DT_NODE_HAS_STATUS(DT_INST(0, nxp_wuu), okay)) {
//			WUU_ClearExternalWakeUpPinsFlag(MCXN_WUU_ADDR, 0xFFFFFFFFu);
//		}
//		/* Measure residency around WFI for quick sanity */
//		uint32_t t0 = k_cycle_get_32();
//		standby_entries++;
		/* Debug: snapshot core/NVIC/SysTick state before WFI 
		{
			uint32_t icsr = SCB->ICSR;
			printk("pre-WFI(powerdown): ICSR=%#x SYST_CTR=%#x LOAD=%u VAL=%u\r\n",
			       icsr, SysTick->CTRL, SysTick->LOAD, SysTick->VAL);
		}*/
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
//		uint32_t dt = k_cycle_get_32() - t0;
//		printk("[SOC standby] WFI residency cycles=%u\r\n", dt);
		break;
/*
	case PM_STATE_SUSPEND_TO_RAM:
		cmc_config.main_domain = kCMC_DeepPowerDown;
		cmc_config.wake_domain = kCMC_DeepPowerDown;
		printk("ready to enter deep power down mode.\r\n");
		CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);
		break;
*/
	default:
		LOG_DBG("Unsupported power state %u", state);
		break;
	}
}

/* Handle SOC specific activity after Low Power Mode Exit */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

     //   printk("PD status 1 flags = %x\r\n", MCXN_SPC_ADDR.SC);
	__enable_irq();
	__ISB();

//	printk("SPC status flags = %x\r\n", SPC0->SC);
//	/* Also log WUU ME/DE on exit */
//	printk("WUU ME=%#x DE=%#x (post-exit)\r\n", WUU0->ME, WUU0->DE);
//	bool final_lptmr = false;
//	uint32_t lptmr_csr_snapshot = 0U;
//	/* Report WUU PF/MF on exit to pinpoint wake sources */
//	uint32_t pf = WUU_GetExternalWakeUpPinsFlag(MCXN_WUU_ADDR);
//	printk("WUU PF=%#x\r\n", pf);
#if defined(FSL_FEATURE_WUU_HAS_MF) && FSL_FEATURE_WUU_HAS_MF
	uint32_t mf_exit = WUU_GetModuleInterruptFlag(MCXN_WUU_ADDR);
	printk("WUU MF=%#x\r\n", mf_exit);
#endif
		/* Decode CKSTAT a bit for readability */
//		uint32_t ckstat = CMC0->CKSTAT;
//		uint8_t wake = CMC_GetWakeupSource(MCXN_CMC_ADDR);
//		cmc_clock_mode_t ckmode = CMC_GetClockMode(MCXN_CMC_ADDR);
//		cmc_core_clock_gate_status_t gated = CMC_GetCoreClockGatedStatus(MCXN_CMC_ADDR);
//		printk("CMC CKSTAT=%#x (wake=%#x ckmode=%u gated=%u)\r\n", ckstat, wake, (unsigned)ckmode, (unsigned)gated);
//		/* Clear the gated status sticky bit for next cycle */
//		CMC_ClearCoreClockGatedStatus(MCXN_CMC_ADDR);
//
//		/* After exit, keep OR masked to avoid noise; pending was consumed by ISR if any. */
//		irq_disable(OR_IRQn);
//		NVIC_ClearPendingIRQ(OR_IRQn);

#ifdef CONFIG_PM_DEBUG
	/* Optional debug: dump and clear LPTMR0 status if present */
	if (DT_NODE_HAS_STATUS(DT_NODELABEL(lptmr0), okay)) {
		LPTMR_Type *lptmr0 = (LPTMR_Type *)DT_REG_ADDR(DT_NODELABEL(lptmr0));
		uint32_t csr = lptmr0->CSR;
		uint32_t cmr = lptmr0->CMR;
		uint32_t psr = lptmr0->PSR;
		printk("LPTMR0 post-exit CSR=%u CMR=%u PSR=%u\r\n", csr, cmr, psr);
		lptmr_csr_snapshot = csr;
		if (csr & LPTMR_CSR_TCF_MASK) {
			final_lptmr = true; /* Compare flag set => real expiration */
		}
		/* If compare flag is set but interrupt path didn’t run, clear it */
		if (csr & LPTMR_CSR_TCF_MASK) {
			LPTMR_ClearStatusFlags(lptmr0, kLPTMR_TimerCompareFlag);
		}
	}
#endif

	/* Classify this wake: CMC wake source 0x4 (WUU) with no LPTMR TCF implies intermediate loop */
//	uint8_t wake_src = CMC_GetWakeupSource(MCXN_CMC_ADDR);
//	if (state == PM_STATE_STANDBY && wake_src == 0x4 && !final_lptmr) {
//		wuu_intermediate_wakes++;
//		printk("[standby] intermediate WUU wake #%u (entries=%u) CSR_TCF=%u\r\n",
//		       wuu_intermediate_wakes, standby_entries, (unsigned)((lptmr_csr_snapshot & LPTMR_CSR_TCF_MASK)!=0));
//	} else if (state == PM_STATE_STANDBY && wake_src == 0x4 && final_lptmr) {
//		printk("[standby] final LPTMR wake after %u intermediate WUU loops (entries=%u)\r\n",
//		       wuu_intermediate_wakes, standby_entries);
//		wuu_intermediate_wakes = 0; /* reset for next cycle */
//		standby_entries = 0;
//	}

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
