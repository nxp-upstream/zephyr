/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/poweroff.h>

#include "power.h"

void z_sys_poweroff(void)
{
	/*
	 * Deep Power Down 2: the VBAT SRAM is not kept. sys_poweroff() has already
	 * locked interrupts. A CSSI denial returns before WFI, and this function
	 * must not return, so try again.
	 */
	for (;;) {
		(void)power_enter_deep_power_down(false);
	}
}
