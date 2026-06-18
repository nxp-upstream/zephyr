/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/drivers/timer/system_timer.h>

#include "power_modes.h"

LOG_MODULE_REGISTER(soc_power, CONFIG_SOC_LOG_LEVEL);

#if defined(CONFIG_SOC_IMXRT7XX_POWER_DOMAIN_COMPUTE)
void __weak board_rt700_dsr_restore(void)
{
}
#endif

/*
 * DEEP_SLEEP_CONFIG
 * [0]: SLEEPCON0 SLEEPCFG: clock / osc / pll
 * [1]: PMC PDSLEEPCFG0: voltage domain (VDD2/VDDN/...)
 * [2]: PMC PDSLEEPCFG1: VD / OTP /ROM
 * [3]: PMC PDSLEEPCFG2: SRAM 0-31
 * [4]: PMC PDSLEEPCFG3: SRAM 32-63
 * [5]: PMC PDSLEEPCFG4: cache / peripheral
 * [6]: PMC PDSLEEPCFG5: others
 */
#define DEEP_SLEEP_CONFIG                                                                          \
	((const uint32_t[])DT_PROP_OR(DT_NODELABEL(deepsleep), deep_sleep_config, {}))

#if defined(CONFIG_SOC_IMXRT7XX_POWER_DOMAIN_COMPUTE)
/* Deep sleep retention (DSR) keep-alive mask, compute domain only. */
#define DEEP_SLEEP_RETENTION_CONFIG                                                                \
	((const uint32_t[])DT_PROP_OR(DT_NODELABEL(dsr), deep_sleep_retention_config, {}))
#endif

/* Sleep mode entry */
void power_enter_sleep(void)
{
	unsigned int key = arch_pm_state_set_prepare();

	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	__WFI();

	arch_pm_state_set_finish(key);
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		power_enter_sleep();
		break;

	case PM_STATE_SUSPEND_TO_IDLE:
		power_enter_deep_sleep(DEEP_SLEEP_CONFIG);
		break;

#if defined(CONFIG_SOC_IMXRT7XX_POWER_DOMAIN_COMPUTE)
	case PM_STATE_STANDBY:
		/*
		 * The OS Timer is powered off in DSR and cannot wake the
		 * system. Hand the pending timeout over to the companion
		 * wakeup counter (the IRTC alarm) before entering: a zero-tick
		 * idle timeout request arms the low-power companion.
		 */
		{
			k_spinlock_key_t key = sys_clock_lock();

			sys_clock_set_timeout(0, true);

			sys_clock_unlock(key);
		}

		power_enter_dsr(DEEP_SLEEP_RETENTION_CONFIG);
		break;
#endif

	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
	ARG_UNUSED(state);
}
