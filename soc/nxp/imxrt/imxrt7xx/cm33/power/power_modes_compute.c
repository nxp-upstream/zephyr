/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RT7xx Compute power domain (PMC0 / SLEEPCON0) low power mode entry.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/arch_interface.h>

#include "power_modes.h"
#include "fsl_common.h"

#if IS_ENABLED(CONFIG_SOC_MIMXRT7XX_PM_XIP_HANDOVER)
#include "power_xip.h"
#endif

#ifndef POWER_DEFAULT_PMICMODE_DS
#define POWER_DEFAULT_PMICMODE_DS 1U
#endif
#ifndef POWER_DEFAULT_PMICMODE_DSR
#define POWER_DEFAULT_PMICMODE_DSR 1U
#endif

/* SLEEPCON0 SLEEPCFG: everything that can be shut off in deep sleep. */
#define SLEEPCON0_DEEP_SLEEP                                                                       \
	(SLEEPCON0_SLEEPCFG_COMP_MAINCLK_SHUTOFF_MASK |                                            \
	 SLEEPCON0_SLEEPCFG_SENSEP_MAINCLK_SHUTOFF_MASK |                                          \
	 SLEEPCON0_SLEEPCFG_SENSES_MAINCLK_SHUTOFF_MASK |                                          \
	 SLEEPCON0_SLEEPCFG_RAM0_CLK_SHUTOFF_MASK | SLEEPCON0_SLEEPCFG_RAM1_CLK_SHUTOFF_MASK |     \
	 SLEEPCON0_SLEEPCFG_COMN_MAINCLK_SHUTOFF_MASK |                                            \
	 SLEEPCON0_SLEEPCFG_MEDIA_MAINCLK_SHUTOFF_MASK | SLEEPCON0_SLEEPCFG_XTAL_PD_MASK |         \
	 SLEEPCON0_SLEEPCFG_FRO0_PD_MASK | SLEEPCON0_SLEEPCFG_FRO1_PD_MASK |                       \
	 SLEEPCON0_SLEEPCFG_FRO2_PD_MASK | SLEEPCON0_SLEEPCFG_LPOSC_PD_MASK |                      \
	 SLEEPCON0_SLEEPCFG_PLLANA_PD_MASK | SLEEPCON0_SLEEPCFG_PLLLDO_PD_MASK |                   \
	 SLEEPCON0_SLEEPCFG_AUDPLLANA_PD_MASK | SLEEPCON0_SLEEPCFG_AUDPLLLDO_PD_MASK |             \
	 SLEEPCON0_SLEEPCFG_ADC0_PD_MASK | SLEEPCON0_SLEEPCFG_FRO0_GATE_MASK |                     \
	 SLEEPCON0_SLEEPCFG_FRO2_GATE_MASK)

#define PCFG0_DEEP_SLEEP                                                                           \
	(PMC_PDSLEEPCFG0_V2DSP_PD_MASK | PMC_PDSLEEPCFG0_V2MIPI_PD_MASK |                          \
	 PMC_PDSLEEPCFG0_V2NMED_DSR_MASK | PMC_PDSLEEPCFG0_VNCOM_DSR_MASK)

#define PCFG0_DSR                                                                                  \
	(PMC_PDSLEEPCFG0_V2COMP_DSR_MASK | PMC_PDSLEEPCFG0_V2NMED_DSR_MASK |                       \
	 PMC_PDSLEEPCFG0_V2COM_DSR_MASK | PMC_PDSLEEPCFG0_VNCOM_DSR_MASK)

#define PCFG1_DEEP_SLEEP                                                                           \
	(PMC_PDSLEEPCFG1_TEMP_PD_MASK | PMC_PDSLEEPCFG1_PMCREF_LP_MASK |                           \
	 PMC_PDSLEEPCFG1_HVD1V8_PD_MASK | PMC_PDSLEEPCFG1_POR1_LP_MASK |                           \
	 PMC_PDSLEEPCFG1_LVD1_LP_MASK | PMC_PDSLEEPCFG1_HVD1_PD_MASK |                             \
	 PMC_PDSLEEPCFG1_AGDET1_PD_MASK | PMC_PDSLEEPCFG1_POR2_LP_MASK |                           \
	 PMC_PDSLEEPCFG1_LVD2_LP_MASK | PMC_PDSLEEPCFG1_HVD2_PD_MASK |                             \
	 PMC_PDSLEEPCFG1_AGDET2_PD_MASK | PMC_PDSLEEPCFG1_PORN_LP_MASK |                           \
	 PMC_PDSLEEPCFG1_LVDN_LP_MASK | PMC_PDSLEEPCFG1_HVDN_PD_MASK |                             \
	 PMC_PDSLEEPCFG1_OTP_PD_MASK | PMC_PDSLEEPCFG1_ROM_PD_MASK |                               \
	 PMC_PDSLEEPCFG1_SRAMSLEEP_MASK)

#define PCFG2_DEEP_SLEEP (0xFFFFFFFFU)
#define PCFG3_DEEP_SLEEP (0xFFFFFFFFU)
#define PCFG4_DEEP_SLEEP (0xFFFFFFFFU)
#define PCFG5_DEEP_SLEEP (0xFFFFFFFFU)

/* Program SLEEPCON0 SLEEPCFG for deep sleep mode. */
static void program_sleepcon_for_deep_sleep(const uint32_t excl[7])
{
	SLEEPCON0->SLEEPCFG = (SLEEPCON0_DEEP_SLEEP & ~excl[0]) | (SLEEPCON0->RUNCFG & ~excl[0]);

	if ((excl[0] & SLEEPCON0_SLEEPCFG_FRO0_PD_MASK) == 0U) {
		SLEEPCON0->PWRDOWN_WAIT &= ~SLEEPCON0_PWRDOWN_WAIT_IGN_FRO0PDR_MASK;
	}
	if ((excl[0] & SLEEPCON0_SLEEPCFG_FRO1_PD_MASK) == 0U) {
		SLEEPCON0->PWRDOWN_WAIT &= ~SLEEPCON0_PWRDOWN_WAIT_IGN_FRO1PDR_MASK;
	}
}

/* Program PMC0 PDSLEEPCFG0 for deep sleep mode. */
static void program_pdsleepcfg0_deep(const uint32_t excl[7])
{
	uint32_t pdsleepcfg0 = PMC0->PDSLEEPCFG0 & ~PCFG0_DEEP_SLEEP;

	pdsleepcfg0 &= ~(PMC_PDSLEEPCFG0_FDSR_MASK | PMC_PDSLEEPCFG0_V2COMP_DSR_MASK |
			 PMC_PDSLEEPCFG0_V2COM_DSR_MASK | PMC_PDSLEEPCFG0_DPD_MASK |
			 PMC_PDSLEEPCFG0_FDPD_MASK | PMC_PDSLEEPCFG0_PMICMODE_MASK);

	PMC0->PDSLEEPCFG0 = pdsleepcfg0 | (PCFG0_DEEP_SLEEP & ~excl[1]);
}

/* Program PMC0 PDSLEEPCFG0 for deep sleep retention (DSR) mode . */
static void program_pdsleepcfg0_dsr(const uint32_t excl[7])
{
	/*
	 * Clear every bit we are about to recompute from the live register so a
	 * stale value cannot leak through. PCFG0_DEEP_SLEEP only covers a subset
	 * of the DSR retention bits (V2NMED_DSR, VNCOM_DSR), so V2COMP_DSR and
	 * V2COM_DSR must be cleared explicitly via PCFG0_DSR. Without this, a
	 * residual V2COM_DSR survives regardless of excl[1], putting the PMC in
	 * an illegal retention state (V2COM_DSR set while its VDD2/VDDN companions
	 * are excluded) that collapses the shared rail and warm-resets CPU0 into
	 * the boot ROM instead of resuming in place.
	 */
	uint32_t pdsleepcfg0 =
		PMC0->PDSLEEPCFG0 & ~(PCFG0_DEEP_SLEEP | PCFG0_DSR | PMC_PDSLEEPCFG0_FDSR_MASK);

	pdsleepcfg0 &= ~(PMC_PDSLEEPCFG0_DPD_MASK | PMC_PDSLEEPCFG0_FDPD_MASK |
			 PMC_PDSLEEPCFG0_PMICMODE_MASK);

	/*
	 * Full deep-sleep retention, matching the MCUXpresso SDK
	 * POWER_EnterLowPower_FullConfig(kPower_DeepSleepRetention). Per RM Table 352
	 * the [FDSR], [DPD] and [FDPD] bits OVERRIDE the individual PD*CFG switches:
	 * the [FDSR] row only requires [FDPD]=0 and [DPD]=0, leaving every other DSR
	 * bit "X" (don't care). So FDSR may legally coexist with V2COMP_DSR; the
	 * [V2COMP_DSR] row's "[FDSR]=0" requirement only applies when V2COMP_DSR is
	 * the governing (compute-only) control. Set FDSR for full retention (console
	 * and all peripherals retained at 0.5 V) plus V2COMP_DSR + V2DSP_PD.
	 */
	PMC0->PDSLEEPCFG0 =
		pdsleepcfg0 |
		((PCFG0_DEEP_SLEEP | PCFG0_DSR | PMC_PDSLEEPCFG0_FDSR_MASK) & ~excl[1]) |
		(PMC_PDSLEEPCFG0_V2COMP_DSR_MASK | PMC_PDSLEEPCFG0_V2DSP_PD_MASK);
}

/* Program PDSLEEPCFG1..5 and clear PMCREF_LP if any monitored source stays on. */
static void program_pdsleepcfg1_to_5(const uint32_t excl[7])
{
	PMC0->PDSLEEPCFG1 = (PCFG1_DEEP_SLEEP & ~excl[2]) | (PMC0->PDRUNCFG1 & ~excl[2]);

	if (((excl[0] & (SLEEPCON0_SLEEPCFG_FRO0_PD_MASK | SLEEPCON0_SLEEPCFG_FRO1_PD_MASK |
			 SLEEPCON0_SLEEPCFG_FRO2_PD_MASK | SLEEPCON0_SLEEPCFG_AUDPLLANA_PD_MASK |
			 SLEEPCON0_SLEEPCFG_AUDPLLLDO_PD_MASK | SLEEPCON0_SLEEPCFG_PLLANA_PD_MASK |
			 SLEEPCON0_SLEEPCFG_PLLLDO_PD_MASK)) != 0U) ||
	    ((excl[2] & (PMC_PDSLEEPCFG1_HVDN_PD_MASK | PMC_PDSLEEPCFG1_LVDN_LP_MASK |
			 PMC_PDSLEEPCFG1_PORN_LP_MASK | PMC_PDSLEEPCFG1_HVD2_PD_MASK |
			 PMC_PDSLEEPCFG1_LVD2_LP_MASK | PMC_PDSLEEPCFG1_POR2_LP_MASK |
			 PMC_PDSLEEPCFG1_HVD1_PD_MASK | PMC_PDSLEEPCFG1_LVD1_LP_MASK |
			 PMC_PDSLEEPCFG1_POR1_LP_MASK)) != 0U)) {
		PMC0->PDSLEEPCFG1 &= ~PMC_PDSLEEPCFG1_PMCREF_LP_MASK;
	}

	PMC0->PDSLEEPCFG2 = (PCFG2_DEEP_SLEEP & ~excl[3]) | (PMC0->PDRUNCFG2 & ~excl[3]);
	PMC0->PDSLEEPCFG3 = (PCFG3_DEEP_SLEEP & ~excl[4]) | (PMC0->PDRUNCFG3 & ~excl[4]);
	PMC0->PDSLEEPCFG4 = (PCFG4_DEEP_SLEEP & ~excl[5]) | (PMC0->PDRUNCFG4 & ~excl[5]);
	PMC0->PDSLEEPCFG5 = (PCFG5_DEEP_SLEEP & ~excl[6]) | (PMC0->PDRUNCFG5 & ~excl[6]);
}

/* Apply PMIC-mode pins and LDO mode bits in PDSLEEPCFG0. */
static void program_pmic_and_regulators(const uint32_t excl[7], uint32_t default_pmic_pins)
{
	uint32_t pmic_mode =
		(excl[1] & PMC_PDSLEEPCFG0_PMICMODE_MASK) >> PMC_PDSLEEPCFG0_PMICMODE_SHIFT;

	if (pmic_mode == 0U) {
		PMC0->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_PMICMODE(default_pmic_pins);
	} else {
		PMC0->PDSLEEPCFG0 |= pmic_mode << PMC_PDSLEEPCFG0_PMICMODE_SHIFT;
	}

	PMC0->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO2_VSEL_MASK;

	if ((PMC0->POWERCFG & PMC_POWERCFG_LDO1PD_MASK) != 0U) {
		PMC0->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO1_MODE_MASK; /* Bypass */
	} else {
		PMC0->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_LDO1_MODE_MASK; /* LP */
	}

	if ((PMC0->POWERCFG & PMC_POWERCFG_LDO2PD_MASK) != 0U) {
		PMC0->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO2_MODE_MASK; /* Bypass */
	} else {
		PMC0->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_LDO2_MODE_MASK; /* LP */
	}
}

/*!
 * @brief Stall HiFi4 if VDD2_DSP is being powered down.
 *
 * @details If V2DSP is about to power down while the DSP is still executing
 * instructions, at the moment of power-off the DSP's bus master may be initiating
 * an AHB transaction → causing bus hang / bus deadlock. Setting DSPSTALL=1 causes
 * the DSP core to stop in a safe state (with no in-flight transactions), allowing
 * the PMC to safely power it down.
 */
static void stall_dsp_if_powered_down(void)
{
	if ((PMC0->PDSLEEPCFG0 & PMC_PDSLEEPCFG0_V2DSP_PD_MASK) != 0U) {
		SYSCON0->DSPSTALL = SYSCON0_DSPSTALL_DSPSTALL_MASK;
	}
}

/*!
 * @brief Clear PMC event/power flags before WFI.
 *
 * @details The PMC uses some sticky flags to mark "last wake reason"
 * and "which rail last triggered a power event". If not cleared, the
 * next time the software wakes up and reads the FLAGS, it will see
 * a mixture of "past + present", causing wake source identification
 * confusion. This also prevents certain flags from remaining set,
 * which could cause spurious triggers in interrupt/event paths.
 */
static void pmc_clear_event_flags(void)
{
	PMC0->FLAGS = PMC0->FLAGS;
	PMC0->PWRFLAGS = PMC0->PWRFLAGS;
}

/*!
 * @brief Save PMC0 CTRL and disable LVD-driven resets across the WFI.
 *
 * @details During Deep Sleep, the regulators switches to LP mode,
 * causing the rail voltage to briefly drop or fluctuate. If the LVD reset
 * enable is still active (LVDxRE), this fluctuation will be detected as
 * "low voltage" => triggering a reset, causing the board to be kicked back
 * to boot. Therefore, temporarily disable the reset enable bit before entering
 * deep sleep, and restore it after waking up.
 */
static uint32_t lvd_save_disable(void)
{
	uint32_t saved = PMC0->CTRL;

	PMC0->CTRL = saved & ~(PMC_CTRL_LVDNRE_MASK | PMC_CTRL_LVD2RE_MASK | PMC_CTRL_LVD1RE_MASK |
			       PMC_CTRL_AGDET2RE_MASK | PMC_CTRL_AGDET1RE_MASK);
	return saved;
}

/*!
 * @brief Clear LVD/AGDET status flags and restore PMC0 CTRL after wake-up.
 *
 * @details
 * 1. Clear LVD/AGDET status flags: The "spurious low-voltage events" caused
 *  by PMIC switching during sleep have been recorded in the FLAGS register
 *  and must be cleared via W1C (Write-1-to-Clear). Otherwise, the next time
 *  software reads them, it will mistakenly interpret them as actual faults.
 *
 * 2. Restore CTRL: Re-enable the LVDxRE/AGDETxRE reset enable bits — outside
 *  of sleep mode, we need LVD to provide actual protection.
 *
 * The sequence is critical: Clear the flags first, then enable RE. Otherwise,
 * if you enable RE first and it sees the old flags, it will immediately trigger
 * a reset.
 */
static void lvd_restore(uint32_t saved_ctrl)
{
	PMC0->FLAGS = PMC_FLAGS_LVDVDD1F_MASK | PMC_FLAGS_LVDVDD2F_MASK | PMC_FLAGS_LVDVDDNF_MASK |
		      PMC_FLAGS_AGDET1F_MASK | PMC_FLAGS_AGDET2F_MASK;
	PMC0->CTRL = saved_ctrl;
}

/*!
 * @brief If this is the first domain entering DS, ignore FRO2/LPOSC PDR.
 *
 * @details FRO2/LPOSC are clock sources shared by compute and sense domain;
 * if only the compute domain enters DS while the sense domain is still running,
 * FRO2 won't actually turn off => PMC will never receive the "power-down ready"
 * signal; when DSSENS=0 (sense hasn't entered DS yet), tell the PMC "don't wait
 * for these two signals, proceed directly". Otherwise, the compute domain gets
 * stuck in the PMC state machine, and WFI won't actually enter DS (only enters
 * normal sleep).
 */
AT_QUICKACCESS_SECTION_CODE(static void arm_first_domain_pdr_ignores(void))
{
	if ((PMC0->STATUS & PMC_STATUS_DSSENS_MASK) == 0U) {
		SLEEPCON0->PWRDOWN_WAIT |= SLEEPCON0_PWRDOWN_WAIT_IGN_FRO2PDR_MASK |
					   SLEEPCON0_PWRDOWN_WAIT_IGN_LPOSCPDR_MASK;
	}
}

/*!
 * @brief Relocate the function to RAM (quick-access SRAM section) for execution,
 * instead of the default XIP flash (instruction fetching via XSPI).
 * This is because it calls power_xip_suspend() in the middle of execution flow,
 * which turns off XSPI/flash.
 */
AT_QUICKACCESS_SECTION_CODE(static void power_enter_common(const uint32_t exclude_from_pd[7],
							   bool dsr))
{
	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

	program_sleepcon_for_deep_sleep(exclude_from_pd);
	if (dsr) {
		program_pdsleepcfg0_dsr(exclude_from_pd);
		program_pmic_and_regulators(exclude_from_pd, POWER_DEFAULT_PMICMODE_DSR);
	} else {
		program_pdsleepcfg0_deep(exclude_from_pd);
		program_pmic_and_regulators(exclude_from_pd, POWER_DEFAULT_PMICMODE_DS);
	}
	program_pdsleepcfg1_to_5(exclude_from_pd);
	stall_dsp_if_powered_down();

	/* Latch the PMC programming above: wait for any in-flight update,
	 * pulse APPLYCFG, then wait for completion before WFI.
	 */
	while ((PMC0->STATUS & PMC_STATUS_BUSY_MASK) != 0U) {
	}
	PMC0->CTRL |= PMC_CTRL_APPLYCFG_MASK;
	while ((PMC0->STATUS & PMC_STATUS_BUSY_MASK) != 0U) {
	}

	pmc_clear_event_flags();
	uint32_t saved_ctrl = lvd_save_disable();

	/* Mask interrupts (and clear BASEPRI) while XIP flash is still live.
	 * arch_pm_state_set_prepare() is resident in XIP flash, so it must
	 * run before power_xip_suspend() tears the XSPI down; otherwise the
	 * instruction fetch would fault on the powered-down flash.
	 */
	unsigned int key = arch_pm_state_set_prepare();

#if IS_ENABLED(CONFIG_SOC_MIMXRT7XX_PM_XIP_HANDOVER)
	/*
	 * Deep-sleep retention always collapses the V2COMP domain, so the XCACHEs
	 * lose their contents and must be flushed and disabled unconditionally
	 * (matching the MCUXpresso kPower_DeepSleepRetention path). For plain deep
	 * sleep only flush an XCACHE whose domain is actually powered down: a cache
	 * kept alive via excl[5] (PDSLEEPCFG4) retains its contents.
	 */
	bool fx1 = dsr || (exclude_from_pd[5] & PMC_PDSLEEPCFG4_CPU0_CCACHE_MASK) == 0U;
	bool fx0 = dsr || (exclude_from_pd[5] & PMC_PDSLEEPCFG4_CPU0_SCACHE_MASK) == 0U;

	/* The XSPI controller is powered down for both deep sleep and deep-sleep
	 * retention (the XSPI0 logic lives in the collapsed V2COMP domain, not in
	 * the retained rail); before WFI the flash must be quiesced, the cache
	 * flushed (if it too is powered down) and the XSPI safely deinitialized.
	 * Otherwise the first instruction fetch after wakeup reads from the dead
	 * XSPI and immediately hardfaults.
	 */
	power_xip_suspend(fx0, fx1);
#endif

	arm_first_domain_pdr_ignores();

	__WFI();

#if IS_ENABLED(CONFIG_SOC_MIMXRT7XX_PM_XIP_HANDOVER)
	/* XSPI reinitialization, cache re-enable, allowing flash to be
	 * instruction-fetchable again. Without this step, the next instruction
	 * will directly trigger a hardfault.
	 */
	power_xip_resume();
#endif

	/*
	 * DSR collapses the V2COMP domain, taking down the console UART and its
	 * FlexComm clocks. Restore them here, while interrupts are still masked
	 * and before arch_pm_state_set_finish() lets any other code run, so the
	 * first post-wake console access cannot fault on the dead peripheral.
	 */
#if defined(CONFIG_SOC_IMXRT7XX_POWER_DOMAIN_COMPUTE)
	if (dsr) {
		board_rt700_dsr_restore();
	}
#endif

	arch_pm_state_set_finish(key);

	lvd_restore(saved_ctrl);

	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

AT_QUICKACCESS_SECTION_CODE(void power_enter_deep_sleep(const uint32_t exclude_from_pd[7]))
{
	power_enter_common(exclude_from_pd, false);
}

AT_QUICKACCESS_SECTION_CODE(void power_enter_dsr(const uint32_t exclude_from_pd[7]))
{
	power_enter_common(exclude_from_pd, true);
}
