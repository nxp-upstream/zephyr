/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_NXP_IMXRT_IMXRT266X_POWER_POWER_H_
#define SOC_NXP_IMXRT_IMXRT266X_POWER_POWER_H_

#include <stdbool.h>

#include <fsl_power.h>

/*
 * Enter Deep Power Down. Its two variants differ only in the VBAT SRAM: Deep
 * Power Down 1 keeps it, Deep Power Down 2 powers it off. The wakeup is a
 * reset, so this returns only when the entry is refused before WFI, with
 * kStatus_Fail.
 */
static inline status_t power_enter_deep_power_down(bool retain_vbat_sram)
{
	const power_deep_power_down_config_t config = {
		.retainVbatSram = retain_vbat_sram,
	};

	return POWER_EnterDeepPowerDown(&config);
}

#endif /* SOC_NXP_IMXRT_IMXRT266X_POWER_POWER_H_ */
