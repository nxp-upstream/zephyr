/*
 * Copyright (c) 2017, 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_KINETIS_SIM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_KINETIS_SIM_H_

#define KINETIS_SIM_CORESYS_CLK		0
#define KINETIS_SIM_PLATFORM_CLK	1
#define KINETIS_SIM_BUS_CLK		2
#define KINETIS_SIM_FAST_PERIPHERAL_CLK	5
#define KINETIS_SIM_LPO_CLK		19
#define KINETIS_SIM_SIM_SOPT7   7
#define KINETIS_SIM_OSCERCLK    8
#define KINETIS_SIM_MCGIRCLK    12
#define KINETIS_SIM_MCGPCLK     18

#define KINETIS_SIM_PLLFLLSEL_MCGFLLCLK	0
#define KINETIS_SIM_PLLFLLSEL_MCGPLLCLK	1
#define KINETIS_SIM_PLLFLLSEL_IRC48MHZ	3

#define KINETIS_SIM_ER32KSEL_OSC32KCLK	0
#define KINETIS_SIM_ER32KSEL_RTC	2
#define KINETIS_SIM_ER32KSEL_LPO1KHZ	3

/*
 * Kinetis SIM clock specifier encoding for consumers that only pass the `name`
 * cell to the clock_control API.
 *
 * Some devicetrees provide a 3-cell SIM clock specifier:
 *
 *   <name offset bits>
 *
 * but many drivers historically pass only the `name` cell (via
 * DT_*_CLOCKS_CELL(..., name)). To allow those drivers to control SIM clock
 * gates and query rates without per-driver changes, the `name` cell can be
 * encoded with both gate and rate information.
 *
 * Layout (32-bit):
 *   bit 31      : 1 (encoded marker)
 *   bits 26..30 : gate bit (0..31)
 *   bits 13..25 : gate register offset (0..0x1FFF)
 *   bits 0..12  : clock name selector (0..0x1FFF)
 */

#define KINETIS_SIM_SUBSYS_ENCODE(name, offset, bits) \
	(0x80000000U | (((bits) & 0x1FU) << 26) | (((offset) & 0x1FFFU) << 13) | \
	 ((name) & 0x1FFFU))

#define KINETIS_SIM_SUBSYS_IS_ENCODED(val) \
	(((val) & 0x80000000U) != 0U)

#define KINETIS_SIM_SUBSYS_DECODE_NAME(val) \
	((val) & 0x1FFFU)

#define KINETIS_SIM_SUBSYS_DECODE_OFFSET(val) \
	(((val) >> 13) & 0x1FFFU)

#define KINETIS_SIM_SUBSYS_DECODE_BITS(val) \
	(((val) >> 26) & 0x1FU)

/*
 * Convenience for keeping SIM clock specifiers readable:
 * expands to the required 3-cell <name offset bits> form while only spelling
 * (offset,bits) once at the call site.
 */
#define KINETIS_SIM_CLOCK_SPEC(name, offset, bits) \
	KINETIS_SIM_SUBSYS_ENCODE(name, offset, bits) offset bits

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_KINETIS_SIM_H_ */
