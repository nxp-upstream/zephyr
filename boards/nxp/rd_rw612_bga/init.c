/*
 * Copyright 2024 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <fsl_power.h>

static void rdrw61x_power_init_config(void)
{
	power_init_config_t initCfg = {
		/* VCORE AVDD18 supplied from iBuck on RD board. */
		.iBuck = true,
		/* CAU_SOC_SLP_REF_CLK needed for LPOSC. */
		.gateCauRefClk = false,
	};

	POWER_InitPowerConfig(&initCfg);
}

#if CONFIG_PM
static void rdrw61x_pm_state_exit(enum pm_state state)
{
	switch (state) {
	case PM_STATE_STANDBY:
		rdrw61x_power_init_config();
		break;
	default:
		break;
	}
}
#endif

static int rdrw61x_init(void)
{
	rdrw61x_power_init_config();

#if CONFIG_PM
	static struct pm_notifier rdrw61x_pm_notifier = {
		.state_exit = rdrw61x_pm_state_exit,
	};

	/* clk_32k not derived from cau. It's safe to disable CAU clock in Power Mode 3, 4. */
	POWER_ConfigCauInSleep(true);

	pm_notifier_register(&rdrw61x_pm_notifier);
#endif
	return 0;
}

SYS_INIT(rdrw61x_init, PRE_KERNEL_1, CONFIG_BOARD_INIT_PRIORITY);
