/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/poweroff.h>
#include <zephyr/toolchain.h>
#include "fsl_cmc.h"
#include "fsl_spc.h"

#if CONFIG_POWEROFF
void z_sys_poweroff(void)
{
    cmc_power_domain_config_t cmc_config = {
        .clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode,
        .main_domain = kCMC_DeepPowerDown,
        .wake_domain = kCMC_DeepPowerDown,
    };

    spc_lowpower_mode_dcdc_option_t spc_config = {
        .DCDCVoltage       = kSPC_DCDC_MidVoltage,
        .DCDCDriveStrength = kSPC_DCDC_LowDriveStrength,
    };

    SPC_SetLowPowerModeDCDCRegulatorConfig(SPC0, &spc_config);
    SPC_SetLowPowerWakeUpDelay(SPC0, 0x30CU);
            

    CMC_SetPowerModeProtection(CMC0, kCMC_AllowDeepPowerDownMode);
    CMC_EnterLowPowerMode(CMC0, &cmc_config);

    CODE_UNREACHABLE;
}
#endif /* CONFIG_POWEROFF */
