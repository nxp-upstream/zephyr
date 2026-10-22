/*
 * SPDX-FileCopyrightText: 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_SFA_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_SFA_H_

/**
 * @file
 * @brief Input selectors for the NXP SFA (Signal Frequency Analyser).
 *
 * The SFA picks its inputs itself, no INPUTMUX involved: REF values go to
 * CTRL2[REF_CLK_SEL] ("reference-select"), CUT values to CTRL[CUT_SEL]
 * ("target-select"). Selector numbers are SoC specific and grouped by SoC.
 */

/*
 * MCXW72 SFA0 (MCX W72 RM rev. 4, tables 216 and 218).
 * Devices: MCXW727A, MCXW727C, MCXW727D (CM33 core 0).
 */

/** Reference: RF reference oscillator (32 MHz). */
#define NXP_SFA_MCXW72_REF_OSC_RF  0
/** Reference: CPU core clock. */
#define NXP_SFA_MCXW72_REF_CPU_CLK 1
/** Reference: 32 kHz clock. */
#define NXP_SFA_MCXW72_REF_32K     2

/** Target: CLKOUT. */
#define NXP_SFA_MCXW72_CUT_CLKOUT        1
/** Target: 32 kHz clock. */
#define NXP_SFA_MCXW72_CUT_32K           6
/** Target: RF reference oscillator (32 MHz). */
#define NXP_SFA_MCXW72_CUT_OSC_RF        8
/** Target: radio FRO-192M, divided. */
#define NXP_SFA_MCXW72_CUT_RADIO_FRO192M 10
/** Target: FRO-16K. */
#define NXP_SFA_MCXW72_CUT_FRO_16K       11

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_SFA_H_ */
