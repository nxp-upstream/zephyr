/*
 * SPDX-FileCopyrightText: 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_FMEAS_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_FMEAS_H_

/*
 * INPUTMUX connection cookies for the NXP BASIC FREQME (fmeas) reference and
 * target inputs. These mirror the kINPUTMUX_<source>ToFreqmeas enumerators in
 * each SoC's fsl_inputmux_connections.h.
 *
 * A cookie encodes the FMEASURE destination mux id (0x700) shifted by the
 * INPUTMUX PMUX_SHIFT (20) OR'ed with the per-source selector value. The same
 * source cookie is valid for either FMEASURE channel; the devicetree
 * "channel" cell selects the role: channel 0 = reference (FMEASURE_CH_SEL[0]),
 * channel 1 = target (FMEASURE_CH_SEL[1]).
 */

#define NXP_FMEAS_PMUX_ID    0x700U
#define NXP_FMEAS_PMUX_SHIFT 20U

#define NXP_FMEAS_CONNECTION(sel) \
	(((sel) & ((1U << NXP_FMEAS_PMUX_SHIFT) - 1U)) | \
	 (NXP_FMEAS_PMUX_ID << NXP_FMEAS_PMUX_SHIFT))

/* RT6xx (MIMXRT685S) - see RT600 fsl_inputmux_connections.h */
#define NXP_FMEAS_RT6XX_XTALIN          NXP_FMEAS_CONNECTION(0)
#define NXP_FMEAS_RT6XX_SFRO            NXP_FMEAS_CONNECTION(1)
#define NXP_FMEAS_RT6XX_FFRO            NXP_FMEAS_CONNECTION(2)
#define NXP_FMEAS_RT6XX_LPOSC          NXP_FMEAS_CONNECTION(3)
#define NXP_FMEAS_RT6XX_RTC_32KHZ      NXP_FMEAS_CONNECTION(4)
#define NXP_FMEAS_RT6XX_MAIN_SYS_CLK   NXP_FMEAS_CONNECTION(5)
#define NXP_FMEAS_RT6XX_FREQME_GPIO_CLK NXP_FMEAS_CONNECTION(6)

/* RT5xx (MIMXRT595S) - see RT500 fsl_inputmux_connections.h */
#define NXP_FMEAS_RT5XX_XTALIN          NXP_FMEAS_CONNECTION(0)
#define NXP_FMEAS_RT5XX_FRO_12M         NXP_FMEAS_CONNECTION(1)
#define NXP_FMEAS_RT5XX_FRO_192M        NXP_FMEAS_CONNECTION(2)
#define NXP_FMEAS_RT5XX_LPOSC          NXP_FMEAS_CONNECTION(3)
#define NXP_FMEAS_RT5XX_OSC_32KHZ      NXP_FMEAS_CONNECTION(4)
#define NXP_FMEAS_RT5XX_MAIN_SYS_CLK   NXP_FMEAS_CONNECTION(5)
#define NXP_FMEAS_RT5XX_FREQME_GPIO_CLK NXP_FMEAS_CONNECTION(6)
#define NXP_FMEAS_RT5XX_CLOCK_OUT      NXP_FMEAS_CONNECTION(11)

/* RW6xx (RW612) - see RW612 fsl_inputmux_connections.h */
#define NXP_FMEAS_RW6XX_SYSOSC          NXP_FMEAS_CONNECTION(0)
#define NXP_FMEAS_RW6XX_SFRO            NXP_FMEAS_CONNECTION(1)
#define NXP_FMEAS_RW6XX_FFRO            NXP_FMEAS_CONNECTION(2)
#define NXP_FMEAS_RW6XX_LPOSC          NXP_FMEAS_CONNECTION(3)
#define NXP_FMEAS_RW6XX_XTAL_32K       NXP_FMEAS_CONNECTION(4)
#define NXP_FMEAS_RW6XX_C0_FR_HCLK     NXP_FMEAS_CONNECTION(5)
#define NXP_FMEAS_RW6XX_FREQME_GPIO_CLK NXP_FMEAS_CONNECTION(6)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_MONITOR_NXP_FMEAS_H_ */
