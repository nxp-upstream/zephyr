/*
 * Copyright (c) 2021 Vestas Wind Systems A/S
 * Copyright 2021, 2024-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <soc.h>

LOG_MODULE_DECLARE(power, CONFIG_PM_LOG_LEVEL);

#ifdef CONFIG_XIP
__ramfunc static void wait_for_flash_prefetch_and_wfi(void)
{
	uint32_t i;

	for (i = 0; i < 8; i++) {
		arch_nop();
	}

	k_cpu_idle();
}
#endif /* CONFIG_XIP */

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	__enable_irq();

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		/* Normal WAIT: WFI with SLEEPDEEP cleared. */
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
		k_cpu_idle();
		break;
	case PM_STATE_SUSPEND_TO_IDLE:
		/*
		 * STOP (deep sleep) entry.
		 *
		 * The devicetree substate-id maps to the PSTOPO field (partial stop option).
		 * Valid values are SoC-specific; for KE1xZ this is typically 0..2.
		 */
		if (substate_id > 2U) {
			LOG_WRN("Unsupported substate-id %u, using 0", substate_id);
			substate_id = 0U;
		}

		/* Ensure STOPM is set to normal STOP (not VLPS/VLLS families), if present. */
		SMC->PMCTRL = (SMC->PMCTRL & ~SMC_PMCTRL_STOPM_MASK) | SMC_PMCTRL_STOPM(0U);

#if (defined(FSL_FEATURE_SMC_HAS_PSTOPO) && FSL_FEATURE_SMC_HAS_PSTOPO)
		SMC->STOPCTRL = (SMC->STOPCTRL & ~SMC_STOPCTRL_PSTOPO_MASK) |
				SMC_STOPCTRL_PSTOPO(substate_id);
#endif

		SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
		/* Read back to ensure writes complete before WFI. */
		(void)SMC->PMCTRL;

		if (IS_ENABLED(CONFIG_XIP)) {
			wait_for_flash_prefetch_and_wfi();
		} else {
			k_cpu_idle();
		}

		if (SMC->PMCTRL & SMC_PMCTRL_STOPA_MASK) {
			LOG_DBG("partial stop aborted");
		}
		break;
	default:
		LOG_WRN("Unsupported power state %u", state);
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	if (state == PM_STATE_SUSPEND_TO_IDLE) {
		/* Disable deep sleep upon exit */
		SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk);
	}

	irq_unlock(0);
}
