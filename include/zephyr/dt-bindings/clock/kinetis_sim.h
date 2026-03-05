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
 *   &lt;name offset bits&gt;
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

/**
 * @def KINETIS_SIM_SUBSYS_ENCODE
 *
 * @brief Encode SIM clock gate and selector into a single 32-bit value.
 *
 * Some clock consumers only pass the first clock specifier cell ("name") to
 * the clock control API. This macro encodes gate offset/bit along with the
 * clock name selector so those consumers can still control SIM gates.
 *
 * @param name Clock selector (KINETIS_SIM_*_CLK)
 * @param offset Gate register offset
 * @param bits Gate bit index (0..31)
 */

#define KINETIS_SIM_SUBSYS_ENCODE(name, offset, bits) \
	(0x80000000U | (((bits) & 0x1FU) << 26) | (((offset) & 0x1FFFU) << 13) | \
	 ((name) & 0x1FFFU))

/**
 * @def KINETIS_SIM_SUBSYS_IS_ENCODED
 *
 * @brief Test whether a subsystem value is encoded.
 *
 * @param val Subsystem value (encoded or legacy)
 */

#define KINETIS_SIM_SUBSYS_IS_ENCODED(val) \
	(((val) & 0x80000000U) != 0U)

/**
 * @def KINETIS_SIM_SUBSYS_DECODE_NAME
 *
 * @brief Extract the clock selector (name) from an encoded subsystem.
 *
 * @param val Encoded subsystem value
 */

#define KINETIS_SIM_SUBSYS_DECODE_NAME(val) \
	((val) & 0x1FFFU)

/**
 * @def KINETIS_SIM_SUBSYS_DECODE_OFFSET
 *
 * @brief Extract the gate register offset from an encoded subsystem.
 *
 * @param val Encoded subsystem value
 */

#define KINETIS_SIM_SUBSYS_DECODE_OFFSET(val) \
	(((val) >> 13) & 0x1FFFU)

/**
 * @def KINETIS_SIM_SUBSYS_DECODE_BITS
 *
 * @brief Extract the gate bit index from an encoded subsystem.
 *
 * @param val Encoded subsystem value
 */

#define KINETIS_SIM_SUBSYS_DECODE_BITS(val) \
	(((val) >> 26) & 0x1FU)

/*
 * Convenience for keeping SIM clock specifiers readable:
 * expands to the required 3-cell &lt;name offset bits&gt; form while only spelling
 * (offset,bits) once at the call site.
 */
/**
 * @def KINETIS_SIM_CLOCK_SPEC
 *
 * @brief Build a 3-cell SIM clock specifier with an encoded name cell.
 *
 * Expands to the required &lt;name offset bits&gt; format for the SIM clock provider,
 * while encoding gate info into the first cell for legacy consumers.
 */
#define KINETIS_SIM_CLOCK_SPEC(name, offset, bits) \
	KINETIS_SIM_SUBSYS_ENCODE(name, offset, bits) offset bits

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_KINETIS_SIM_H_ */
