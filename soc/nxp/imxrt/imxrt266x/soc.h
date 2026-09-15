/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC__H_
#define _SOC__H_

#include <zephyr/sys/util.h>

#ifndef _ASMLANGUAGE

#include <fsl_common.h>

/* Add include for DTS generated information */
#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bring-up steps that can give up, reported as the fatal-error reason. The
 * console does not exist yet when they run, so the identifier is the whole
 * diagnostic.
 */
enum soc_early_init_step {
	SOC_STEP_ELE_SEND,
	SOC_STEP_ELE_RECEIVE,
	SOC_STEP_ELE_REFUSED,
	SOC_STEP_LLC_TAG_INIT,
	SOC_STEP_LLC_DATA_INIT,
};

FUNC_NORETURN void soc_early_init_failed(enum soc_early_init_step step);

/*
 * Bound on the pre-console polling loops. A bounded loop that reports through
 * the fatal path names the step that gave up, where a hang would not. A
 * liveness guard, not a timing specification.
 */
#define SOC_POLL_LIMIT 1000000U

#define SOC_POLL_UNTIL(cond, step)                                                                 \
	do {                                                                                       \
		uint32_t _spins = SOC_POLL_LIMIT;                                                  \
                                                                                                   \
		while (!(cond)) {                                                                  \
			if (--_spins == 0U) {                                                      \
				soc_early_init_failed(step);                                       \
			}                                                                          \
		}                                                                                  \
	} while (false)

/* Hand the Resource Domain Controller to the CPU's domain. Call first. */
void soc_trdc_setup(void);

/*
 * Give every TRDC master the CPU's domain. Touches MEDIA__TRDC, which requires
 * mediabus_rootclk to be running -- call after soc_clock_init().
 */
void soc_trdc_assign_masters(void);

/* MIPI-DSI attach/detach hooks the driver calls; see display_if.c. */
void imxrt_pre_init_display_interface(void);
void imxrt_post_init_display_interface(void);
void imxrt_deinit_display_interface(void);

/*
 * MIPI-DSI DPHY bit-clock root, configured once at early boot by
 * soc_early_init_hook() (see soc_mipi_dsi_clock_init() in soc.c) rather than
 * computed/configured by dsi_mcux_split.c itself. Boards/shields declare a
 * phy-clock devicetree property matching this fixed value; that driver uses
 * it for D-PHY timing math and its own bandwidth sanity check.
 */
#define SOC_MIPI_DSI_BIT_CLK_HZ MHZ(400)

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */

#endif /* _SOC__H_ */
