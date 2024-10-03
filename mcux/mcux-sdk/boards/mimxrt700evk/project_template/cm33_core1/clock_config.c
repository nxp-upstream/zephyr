/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/***********************************************************************************************************************
 * This file was generated by the MCUXpresso Config Tools. Any manual edits made to this file
 * will be overwritten if the respective MCUXpresso Config Tools is used to update this file.
 **********************************************************************************************************************/
/*
 * How to set up clock using clock driver functions:
 *
 * 1. Setup clock sources.
 *
 * 2. Set up all selectors to provide selected clocks.
 *
 * 3. Set up all dividers.
 */

#include "fsl_clock.h"
#include "clock_config.h"
#include "fsl_power.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
/*******************************************************************************
 ************************ BOARD_InitBootClocks function ************************
 ******************************************************************************/
void BOARD_InitBootClocks(void)
{
    BOARD_BootClockRUN();
}

/*******************************************************************************
 ********************** Configuration BOARD_BootClockRUN ***********************
 ******************************************************************************/

/*******************************************************************************
 * Variables for BOARD_BootClockRUN configuration
 ******************************************************************************/

/*******************************************************************************
 * Code for BOARD_BootClockRUN configuration
 ******************************************************************************/
void BOARD_BootClockRUN(void)
{
    /* Power up OSC in case it's not enabled. */
    POWER_DisablePD(kPDRUNCFG_PD_SYSXTAL);
    CLOCK_EnableSysOscClk(true, true, BOARD_SYSOSC_SETTLING_US); /* Enable system OSC */
    CLOCK_SetXtalFreq(BOARD_XTAL_SYS_CLK_HZ);                    /* Sets external XTAL OSC freq */

    CLOCK_AttachClk(kFRO1_DIV3_to_SENSE_BASE);
    CLOCK_SetClkDiv(kCLOCK_DivSenseMainClk, 1);
    CLOCK_AttachClk(kSENSE_BASE_to_SENSE_MAIN);

    POWER_DisablePD(kPDRUNCFG_GATE_FRO2);
    CLOCK_EnableFroClkFreq(FRO2, 300000000U, kCLOCK_FroAllOutEn);

    CLOCK_EnableFro2ClkForDomain(kCLOCK_AllDomainEnable);

    CLOCK_AttachClk(kFRO2_DIV3_to_SENSE_BASE);
    CLOCK_SetClkDiv(kCLOCK_DivSenseMainClk, 1);
    CLOCK_AttachClk(kSENSE_BASE_to_SENSE_MAIN);

    SystemCoreClock = BOARD_BOOTCLOCKRUN_CORE_CLOCK;
}
