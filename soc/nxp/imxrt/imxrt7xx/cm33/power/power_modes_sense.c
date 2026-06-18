/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RT7xx Sense power domain (PMC1 / SLEEPCON1) low power mode entry.
 *
 * The Sense domain has no DSR mode and no XIP/cache responsibility.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/arch_interface.h>

#include "power_modes.h"
#include "fsl_common.h"

#ifndef POWER_DEFAULT_PMICMODE_DS
#define POWER_DEFAULT_PMICMODE_DS 1U
#endif

/* SLEEPCON1 SLEEPCFG: everything that can be shut off in deep sleep. */
#define SLEEPCON1_DEEP_SLEEP                                                                       \
	(SLEEPCON1_SLEEPCFG_SENSEP_MAINCLK_SHUTOFF_MASK |                                          \
	 SLEEPCON1_SLEEPCFG_SENSES_MAINCLK_SHUTOFF_MASK |                                          \
	 SLEEPCON1_SLEEPCFG_RAM0_CLK_SHUTOFF_MASK | SLEEPCON1_SLEEPCFG_RAM1_CLK_SHUTOFF_MASK |     \
	 SLEEPCON1_SLEEPCFG_COMN_MAINCLK_SHUTOFF_MASK |                                            \
	 SLEEPCON1_SLEEPCFG_MEDIA_MAINCLK_SHUTOFF_MASK | SLEEPCON1_SLEEPCFG_XTAL_PD_MASK |         \
	 SLEEPCON1_SLEEPCFG_FRO2_PD_MASK | SLEEPCON1_SLEEPCFG_LPOSC_PD_MASK |                      \
	 SLEEPCON1_SLEEPCFG_PLLANA_PD_MASK | SLEEPCON1_SLEEPCFG_PLLLDO_PD_MASK |                   \
	 SLEEPCON1_SLEEPCFG_AUDPLLANA_PD_MASK | SLEEPCON1_SLEEPCFG_AUDPLLLDO_PD_MASK |             \
	 SLEEPCON1_SLEEPCFG_ADC0_PD_MASK | SLEEPCON1_SLEEPCFG_FRO2_GATE_MASK)

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

/* Program SLEEPCON1 SLEEPCFG and FRO2 power-down-ready ignore. */
static void program_sleepcon_for_deep_sleep(const uint32_t excl[7])
{
	SLEEPCON1->SLEEPCFG = (SLEEPCON1_DEEP_SLEEP & ~excl[0]) | (SLEEPCON1->RUNCFG & ~excl[0]);

	if ((excl[0] & SLEEPCON1_SLEEPCFG_FRO2_PD_MASK) != 0U) {
		SLEEPCON1->PWRDOWN_WAIT |= SLEEPCON1_PWRDOWN_WAIT_IGN_FRO2PDR_MASK;
	}
}

/* Build PMC1 PDSLEEPCFG0 for deep sleep. */
static void program_pdsleepcfg0_deep(const uint32_t excl[7])
{
	uint32_t pdsleepcfg0 = PMC1->PDSLEEPCFG0 & ~PCFG0_DEEP_SLEEP;

	pdsleepcfg0 &= ~(PMC_PDSLEEPCFG0_FDSR_MASK | PMC_PDSLEEPCFG0_DPD_MASK |
			 PMC_PDSLEEPCFG0_FDPD_MASK | PMC_PDSLEEPCFG0_PMICMODE_MASK);

	PMC1->PDSLEEPCFG0 = pdsleepcfg0 | ((PCFG0_DEEP_SLEEP | PCFG0_DSR) & ~excl[1]);
}

/* Program PDSLEEPCFG1..5 and clear PMCREF_LP if any monitored source stays on. */
static void program_pdsleepcfg1_to_5(const uint32_t excl[7])
{
	PMC1->PDSLEEPCFG1 = (PCFG1_DEEP_SLEEP & ~excl[2]) | (PMC1->PDRUNCFG1 & ~excl[2]);

	if (((excl[0] & (SLEEPCON1_SLEEPCFG_FRO2_PD_MASK | SLEEPCON1_SLEEPCFG_AUDPLLANA_PD_MASK |
			 SLEEPCON1_SLEEPCFG_AUDPLLLDO_PD_MASK | SLEEPCON1_SLEEPCFG_PLLANA_PD_MASK |
			 SLEEPCON1_SLEEPCFG_PLLLDO_PD_MASK)) != 0U) ||
	    ((excl[2] & (PMC_PDSLEEPCFG1_HVDN_PD_MASK | PMC_PDSLEEPCFG1_LVDN_LP_MASK |
			 PMC_PDSLEEPCFG1_PORN_LP_MASK | PMC_PDSLEEPCFG1_HVD2_PD_MASK |
			 PMC_PDSLEEPCFG1_LVD2_LP_MASK | PMC_PDSLEEPCFG1_POR2_LP_MASK |
			 PMC_PDSLEEPCFG1_HVD1_PD_MASK | PMC_PDSLEEPCFG1_LVD1_LP_MASK |
			 PMC_PDSLEEPCFG1_POR1_LP_MASK)) != 0U)) {
		PMC1->PDSLEEPCFG1 &= ~PMC_PDSLEEPCFG1_PMCREF_LP_MASK;
	}

	PMC1->PDSLEEPCFG2 = (PCFG2_DEEP_SLEEP & ~excl[3]) | (PMC1->PDRUNCFG2 & ~excl[3]);
	PMC1->PDSLEEPCFG3 = (PCFG3_DEEP_SLEEP & ~excl[4]) | (PMC1->PDRUNCFG3 & ~excl[4]);
	PMC1->PDSLEEPCFG4 = (PCFG4_DEEP_SLEEP & ~excl[5]) | (PMC1->PDRUNCFG4 & ~excl[5]);
	PMC1->PDSLEEPCFG5 = (PCFG5_DEEP_SLEEP & ~excl[6]) | (PMC1->PDRUNCFG5 & ~excl[6]);
}

/* Apply PMIC-mode pins and LDO mode bits in PDSLEEPCFG0. */
static void program_pmic_and_regulators(const uint32_t excl[7], uint32_t default_pmic_pins)
{
	uint32_t pmic_mode =
		(excl[1] & PMC_PDSLEEPCFG0_PMICMODE_MASK) >> PMC_PDSLEEPCFG0_PMICMODE_SHIFT;

	if (pmic_mode == 0U) {
		PMC1->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_PMICMODE(default_pmic_pins);
	} else {
		PMC1->PDSLEEPCFG0 |= pmic_mode << PMC_PDSLEEPCFG0_PMICMODE_SHIFT;
	}

	PMC1->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO2_VSEL_MASK;

	if ((PMC1->POWERCFG & PMC_POWERCFG_LDO1PD_MASK) != 0U) {
		PMC1->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO1_MODE_MASK;
	} else {
		PMC1->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_LDO1_MODE_MASK;
	}

	if ((PMC1->POWERCFG & PMC_POWERCFG_LDO2PD_MASK) != 0U) {
		PMC1->PDSLEEPCFG0 &= ~PMC_PDSLEEPCFG0_LDO2_MODE_MASK;
	} else {
		PMC1->PDSLEEPCFG0 |= PMC_PDSLEEPCFG0_LDO2_MODE_MASK;
	}
}

/* Clear PMC event/power flags before WFI. */
static void pmc_clear_event_flags(void)
{
	PMC1->FLAGS = PMC1->FLAGS;
	PMC1->PWRFLAGS = PMC1->PWRFLAGS;
}

/* Save PMC1 CTRL and disable LVD-driven resets across the WFI. */
static uint32_t lvd_save_disable(void)
{
	uint32_t saved = PMC1->CTRL;

	PMC1->CTRL = saved & ~(PMC_CTRL_LVDNRE_MASK | PMC_CTRL_LVD2RE_MASK | PMC_CTRL_LVD1RE_MASK |
			       PMC_CTRL_AGDET2RE_MASK | PMC_CTRL_AGDET1RE_MASK);
	return saved;
}

/* Clear LVD/AGDET status flags and restore PMC1 CTRL after wake-up. */
static void lvd_restore(uint32_t saved_ctrl)
{
	PMC1->FLAGS = PMC_FLAGS_LVDVDD1F_MASK | PMC_FLAGS_LVDVDD2F_MASK | PMC_FLAGS_LVDVDDNF_MASK |
		      PMC_FLAGS_AGDET1F_MASK | PMC_FLAGS_AGDET2F_MASK;
	PMC1->CTRL = saved_ctrl;
}

void power_enter_deep_sleep(const uint32_t exclude_from_pd[7])
{
	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

	program_sleepcon_for_deep_sleep(exclude_from_pd);
	program_pdsleepcfg0_deep(exclude_from_pd);
	program_pmic_and_regulators(exclude_from_pd, POWER_DEFAULT_PMICMODE_DS);
	program_pdsleepcfg1_to_5(exclude_from_pd);

	/* Latch the PMC programming above: wait for any in-flight update,
	 * pulse APPLYCFG, then wait for completion before WFI.
	 */
	while ((PMC1->STATUS & PMC_STATUS_BUSY_MASK) != 0U) {
	}
	PMC1->CTRL |= PMC_CTRL_APPLYCFG_MASK;
	while ((PMC1->STATUS & PMC_STATUS_BUSY_MASK) != 0U) {
	}

	pmc_clear_event_flags();
	uint32_t saved_ctrl = lvd_save_disable();

	/* Sense domain unconditionally ignores FRO2/LPOSC PDR. */
	SLEEPCON1->PWRDOWN_WAIT |=
		SLEEPCON1_PWRDOWN_WAIT_IGN_FRO2PDR_MASK | SLEEPCON1_PWRDOWN_WAIT_IGN_LPOSCPDR_MASK;

	unsigned int key = arch_pm_state_set_prepare();

	__WFI();

	arch_pm_state_set_finish(key);

	lvd_restore(saved_ctrl);

	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}
