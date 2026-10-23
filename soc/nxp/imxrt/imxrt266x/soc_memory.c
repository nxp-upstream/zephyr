/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/toolchain.h>

#include "soc_memory.h"

#include <fsl_power.h>

/* The mode reaches the HAL through a plain cast, so the two enums must agree. */
BUILD_ASSERT((int)SOC_MEMORY_MODE_ACTIVE == (int)kMEMCON_PowerModeActive);
BUILD_ASSERT((int)SOC_MEMORY_MODE_RETENTION == (int)kMEMCON_PowerModeRetention);
BUILD_ASSERT((int)SOC_MEMORY_MODE_POWER_DOWN == (int)kMEMCON_PowerModePowerDown);

int soc_memory_set_mode(uint32_t mem_id, enum soc_memory_mode mode)
{
	status_t status;

	status = POWER_SetMemoryRunMode((power_memory_t)mem_id, (memcon_power_mode_t)mode);

	return (status == kStatus_Success) ? 0 : -EINVAL;
}
