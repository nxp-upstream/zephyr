/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/poweroff.h>
#include <zephyr/toolchain.h>
#include "fsl_cmc.h"
#include "fsl_spc.h"
#include "fsl_vbat.h"
#include "fsl_wuu.h"
#include <stdio.h>

#define APP_RAM_ARRAYS_PD (0x13000200)

#if CONFIG_POWEROFF
void z_sys_poweroff(void)
{
    cmc_power_domain_config_t cmc_config = {
        .clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode,
//        .main_domain = kCMC_DeepSleepMode,
//        .wake_domain = kCMC_DeepSleepMode,
        .main_domain = kCMC_PowerDownMode,
        .wake_domain = kCMC_PowerDownMode,
//        .main_domain = kCMC_DeepPowerDown,
//        .wake_domain = kCMC_DeepPowerDown,
    };

    /* Should move to board.c */
    VBAT_EnableFRO16k(VBAT0, false);
    VBAT_UngateFRO16k(VBAT0, kCLOCK_Clk16KToVsys);

    WUU_SetInternalWakeUpModulesConfig(WUU0, 6U, kWUU_InternalModuleInterrupt);
    
    SPC_SetLowPowerWakeUpDelay(SPC0, 0x637);
    SPC0->LP_CFG = ((SPC0->LP_CFG & ~SPC_LP_CFG_BGMODE_MASK) | SPC_LP_CFG_BGMODE(1U));
    CMC_EnableDebugOperation(CMC0, false);
    CMC_ConfigFlashMode(CMC0, true, false);
    CMC_PowerOffSRAMLowPowerOnly(CMC0, APP_RAM_ARRAYS_PD);
//    CMC_SetPowerModeProtection(CMC0, kCMC_AllowDeepSleepMode);
    CMC_SetPowerModeProtection(CMC0, kCMC_AllowPowerDownMode);
//    CMC_SetPowerModeProtection(CMC0, kCMC_AllowDeepPowerDownMode);

    CMC_EnterLowPowerMode(CMC0, &cmc_config);
    
    printf("srs 2 = %x \n", CMC0->SRS);
    printf("ssrs 2 = %x \n", CMC0->SSRS);
    
    printf("wakeup \n");

    CODE_UNREACHABLE;
}
#endif /* CONFIG_POWEROFF */
