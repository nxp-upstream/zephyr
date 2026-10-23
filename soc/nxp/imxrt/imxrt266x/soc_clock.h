/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC_CLOCK_H_
#define _SOC_CLOCK_H_

#include <fsl_power.h>

/**
 * @brief Bring the SoC clock tree up to the Over Drive Run operating point.
 *
 * Programs the analog sources through the power-mode transition a frequency
 * change has to be bracketed by. Call once before any driver: the per-peripheral
 * roots in devicetree are applied later by the clock controller and assume these
 * sources.
 */
void soc_clock_init(void);

/**
 * @brief Switch the running clock tree to the Over Drive Run (HP Run) rates.
 *
 * The rates soc_clock_init() programs: the CPU at the cpu0 devicetree rate. A
 * clock callback for POWER_EnterHpRun(), which orders the core voltage around
 * it. Not reentrant; call only after soc_clock_init().
 */
void soc_clock_switch_to_od_run(void);

/**
 * @brief Switch the running clock tree to the Normal Run rates.
 *
 * The CPU runs from the core PLL at its devicetree rate, the NPU at 666 MHz. A
 * clock callback for POWER_EnterNormalRun(). Not reentrant; call only after
 * soc_clock_init().
 */
void soc_clock_switch_to_normal_run(void);

/**
 * @brief Switch the running clock tree to the Low Power Run rates.
 *
 * The CPU runs from the core PLL re-locked at 600 MHz, the NPU at 400 MHz. A
 * clock callback for POWER_EnterLpRun(). Not reentrant; call only after
 * soc_clock_init().
 */
void soc_clock_switch_to_lp_run(void);

/**
 * @brief Reprogram the clock roots a power domain loses while it is powered off.
 *
 * soc_clock_init() programs the domain-base roots of the COMM and MEDIA domains
 * in the clock controller inside each of them, so powering the domain off
 * returns those roots to their reset values. The NPU root is in the SYSCON
 * clock controller, which stays powered, so there is nothing to reprogram for
 * the NPU domain or for any other domain.
 *
 * @param domain Domain that has just been powered on.
 */
void soc_clock_restore_domain_roots(power_domain_t domain);

#endif /* _SOC_CLOCK_H_ */
