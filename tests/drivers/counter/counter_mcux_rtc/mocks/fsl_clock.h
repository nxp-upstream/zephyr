/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#if !defined(TEST_HAL_MOCKS)
#include_next <fsl_clock.h>
#else

#ifndef TEST_FSL_CLOCK_H_
#define TEST_FSL_CLOCK_H_

struct test_osc32_registers {
	volatile uint8_t CR;
};

struct test_sim_registers {
	volatile uint32_t CHIPCTL;
};

static struct test_osc32_registers osc32_registers;
static struct test_sim_registers sim_registers;
static unsigned int clock_enable_calls;

static inline struct test_osc32_registers *read_osc32(void);

#undef OSC32
#define OSC32 read_osc32()
#ifndef OSC32_CR_ROSCSTB_MASK
#define OSC32_CR_ROSCSTB_MASK 0x20U
#endif

#undef SIM
#define SIM (&sim_registers)
#undef SIM_CHIPCTL_RTC32KCLKSEL_MASK
#undef SIM_CHIPCTL_RTC_CLKSEL_MASK
#undef SIM_CHIPCTL_RTC32KCLKSEL_SHIFT
#undef SIM_CHIPCTL_RTC_CLKSEL_SHIFT

#if TEST_MUX == 1
#define SIM_CHIPCTL_RTC32KCLKSEL_MASK 0xC0000U
#define SIM_CHIPCTL_RTC32KCLKSEL_SHIFT 18U
#elif TEST_MUX == 2
#define SIM_CHIPCTL_RTC_CLKSEL_MASK 0x3U
#define SIM_CHIPCTL_RTC_CLKSEL_SHIFT 0U
#endif

#define kCLOCK_RtcOsc0 0
#define CLOCK_EnableClock test_clock_enable

static inline void CLOCK_EnableClock(int clock)
{
	zassert_equal(clock, kCLOCK_RtcOsc0);
	clock_enable_calls++;
}

#endif /* TEST_FSL_CLOCK_H_ */
#endif /* TEST_HAL_MOCKS */
