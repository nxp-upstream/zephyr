/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for the NXP MCXC454 SoC
 *
 * MCXC454 is a Cortex-M23 part with SCG/MRCC clocking and an FMU flash
 * controller, unlike the Cortex-M0+ MCXC parts handled by soc.c, which use
 * MCG-Lite/SIM clocking and an FTFx controller. Peripheral clock gating and
 * the core clock are set up from the board early-init hook, following the
 * MCXA parts this SoC derives from, so there is no SoC-level clock_init()
 * here.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>

#ifdef CONFIG_SOC_RESET_HOOK

void soc_reset_hook(void)
{
	SystemInit();
}

#endif /* CONFIG_SOC_RESET_HOOK */
