/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/platform/hooks.h>
#include <zephyr/arch/common/pm_s2ram.h>
#include <soc.h>
#include <cmsis_core.h>
#include <fsl_cmc.h>
#include <fsl_spc.h>
#include <fsl_vbat.h>

#define MCXN_CMC_ADDR		(CMC_Type *)DT_REG_ADDR(DT_INST(0, nxp_cmc))
#define MCXN_SPC_ADDR		(SPC_Type *)DT_REG_ADDR(DT_INST(0, nxp_spc))
#define MCXN_VBAT_ADDR		(VBAT_Type *)DT_REG_ADDR(DT_INST(0, nxp_vbat))
#define MCXN_WAKEUP_DELAY	DT_PROP_OR(DT_NODELABEL(spc), wakeup_delay, 0)

/* All four 8 KB RAMA arrays. */
#define MCXN_RAMA_ALL	(kVBAT_SramArray0 | kVBAT_SramArray1 | \
			 kVBAT_SramArray2 | kVBAT_SramArray3)

/*
 * Deep Power Down (mapped to PM_STATE_SUSPEND_TO_RAM) power gates the whole
 * CORE domain, including the ARM System Control Space (NVIC, SCB). The chip
 * wakes through the reset routine; arch_pm_s2ram_resume() then returns directly
 * to the suspend call site without re-running kernel/CPU initialization, so the
 * SCS state is saved here before entry and restored on resume.
 */
static struct {
	uint32_t iser[ARRAY_SIZE(((NVIC_Type *)NVIC_BASE)->ISER)];
	uint32_t ipr[(sizeof(((NVIC_Type *)NVIC_BASE)->IPR)) / sizeof(uint32_t)];
	uint32_t vtor;
	uint32_t scr;
	uint32_t ccr;
	uint32_t shpr[ARRAY_SIZE(((SCB_Type *)SCB_BASE)->SHPR)];
	uint32_t shcsr;
	uint32_t cpacr;
} mcxn_scs_context;

static void mcxn_scs_save(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.iser); i++) {
		mcxn_scs_context.iser[i] = NVIC->ISER[i];
	}

	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.ipr); i++) {
		mcxn_scs_context.ipr[i] = ((volatile uint32_t *)NVIC->IPR)[i];
	}

	mcxn_scs_context.vtor = SCB->VTOR;
	mcxn_scs_context.scr = SCB->SCR;
	mcxn_scs_context.ccr = SCB->CCR;

	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.shpr); i++) {
		mcxn_scs_context.shpr[i] = SCB->SHPR[i];
	}

	mcxn_scs_context.shcsr = SCB->SHCSR;
	mcxn_scs_context.cpacr = SCB->CPACR;
}

static void mcxn_scs_restore(void)
{
	SCB->VTOR = mcxn_scs_context.vtor;
	SCB->CCR = mcxn_scs_context.ccr;
	SCB->CPACR = mcxn_scs_context.cpacr;

	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.shpr); i++) {
		SCB->SHPR[i] = mcxn_scs_context.shpr[i];
	}

	SCB->SHCSR = mcxn_scs_context.shcsr;
	SCB->SCR = mcxn_scs_context.scr;
	__DSB();
	__ISB();

	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.ipr); i++) {
		((volatile uint32_t *)NVIC->IPR)[i] = mcxn_scs_context.ipr[i];
	}

	for (size_t i = 0; i < ARRAY_SIZE(mcxn_scs_context.iser); i++) {
		NVIC->ISER[i] = mcxn_scs_context.iser[i];
	}
}

static int mcxn_s2ram_retain_sram(void)
{
	if (!VBAT_CheckFRO16kEnabled(MCXN_VBAT_ADDR)) {
		VBAT_EnableFRO16k(MCXN_VBAT_ADDR, true);
	}

	VBAT_UngateFRO16k(MCXN_VBAT_ADDR, kVBAT_EnableClockToVddSys);

	if (!VBAT_CheckBandgapEnabled(MCXN_VBAT_ADDR)) {
		(void)VBAT_EnableBandgap(MCXN_VBAT_ADDR, true);
	}

	/* Enable refresh mode to save power */
	VBAT_EnableBandgapRefreshMode(MCXN_VBAT_ADDR, true);

	(void)VBAT_EnableBackupSRAMRegulator(MCXN_VBAT_ADDR, true);
	VBAT_RetainSRAMsInLowPowerModes(MCXN_VBAT_ADDR, MCXN_RAMA_ALL);

	return 0;
}

static int mcxn_enter_deep_power_down(void)
{
	const cmc_power_domain_config_t cmc_config = {
		.clock_mode = kCMC_GateAllSystemClocksEnterLowPowerMode,
		.main_domain = kCMC_DeepPowerDown,
		.wake_domain = kCMC_DeepPowerDown,
	};

	CMC_EnterLowPowerMode(MCXN_CMC_ADDR, &cmc_config);

	return -EBUSY;
}

void mcxn_pm_suspend_to_ram(void)
{
	SPC_SetLowPowerWakeUpDelay(MCXN_SPC_ADDR, MCXN_WAKEUP_DELAY);
	mcxn_s2ram_retain_sram();
	mcxn_scs_save();

	(void)arch_pm_s2ram_suspend(mcxn_enter_deep_power_down);

	/* re-enable aGDET/dGDET; re-disable RAM ECC */
	SystemInit();

	/* re-build clock tree */
	board_early_init_hook();

	/* idle-thread stack is nearly full, it is easy to reach the
	 * PSPLIM limit on resume. Drop the limit here, the next context
	 * switch re-establishes the per-thread PSPLIM.
	 */
	__set_PSPLIM(0U);

	mcxn_scs_restore();
}
