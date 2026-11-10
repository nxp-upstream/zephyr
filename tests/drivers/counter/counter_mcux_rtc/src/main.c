/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>

#define TEST_HAL_MOCKS
#include <fsl_rtc.h>
#include <fsl_clock.h>

#define STARTUP_MS 4
#define STARTUP_TICKS k_ms_to_ticks_ceil64(STARTUP_MS)

static uint64_t now;
static uint64_t stable_at;
static uint64_t read_delay;
static uint64_t forced_sleep;
static uint64_t sleep_limit;
static unsigned int sleep_calls;
static unsigned int busy_wait_calls;
static unsigned int irq_enable_calls;

static void advance_time(uint64_t ticks)
{
	now += ticks;
	if (now >= stable_at) {
		osc32_registers.CR |= OSC32_CR_ROSCSTB_MASK;
	}
}

static inline struct test_osc32_registers *read_osc32(void)
{
	advance_time(read_delay);
	read_delay = 0;
	return &osc32_registers;
}

static inline k_timepoint_t mock_timepoint_calc(k_timeout_t timeout)
{
	return (k_timepoint_t){.tick = now + timeout.ticks};
}

static inline k_timeout_t mock_timepoint_timeout(k_timepoint_t deadline)
{
	return K_TICKS(deadline.tick > now ? deadline.tick - now : 0);
}

static inline bool mock_timepoint_expired(k_timepoint_t deadline)
{
	return K_TIMEOUT_EQ(mock_timepoint_timeout(deadline), K_NO_WAIT);
}

static inline int32_t mock_sleep(k_timeout_t timeout)
{
	zassert_true(++sleep_calls <= STARTUP_MS + 1, "unbounded sleep loop");
	advance_time(forced_sleep != 0 ? forced_sleep : MIN(timeout.ticks, sleep_limit));
	return 0;
}

static inline int32_t mock_msleep(int32_t milliseconds)
{
	return mock_sleep(K_MSEC(milliseconds));
}

static inline void mock_busy_wait(uint32_t microseconds)
{
	zassert_true(++busy_wait_calls <= STARTUP_MS, "unbounded busy wait");
	zassert_equal(microseconds, USEC_PER_MSEC);
	advance_time(k_us_to_ticks_ceil64(microseconds));
}

static inline void mock_irq_enable(unsigned int irq)
{
	ARG_UNUSED(irq);
	irq_enable_calls++;
}

#undef DT_INST_PROP
#define DT_INST_PROP(inst, prop) UTIL_CAT(TEST_PROP_, prop)
#define TEST_PROP_oscillator_startup_time_ms STARTUP_MS
#define TEST_PROP_clock_frequency 32768
#define TEST_PROP_prescaler 32768
#undef DT_INST_ENUM_IDX
#define DT_INST_ENUM_IDX(inst, prop) IS_ENABLED(TEST_LPO)
#undef DT_INST_REG_ADDR
#define DT_INST_REG_ADDR(inst) ((uintptr_t)&rtc_registers)
#undef DT_INST_IRQN
#define DT_INST_IRQN(inst) 0
#undef DT_INST_IRQ
#define DT_INST_IRQ(inst, cell) 0
#undef DEVICE_DT_INST_DEFINE
#define DEVICE_DT_INST_DEFINE(...)
#undef IRQ_CONNECT
#define IRQ_CONNECT(irq, priority, isr, ...) ARG_UNUSED(isr)

#define k_msleep mock_msleep
#define k_sleep mock_sleep
#define k_busy_wait mock_busy_wait
#undef irq_enable
#define irq_enable mock_irq_enable
#define sys_timepoint_calc mock_timepoint_calc
#define sys_timepoint_timeout mock_timepoint_timeout
#define sys_timepoint_expired mock_timepoint_expired

#if defined(TEST_NO_TIMER)
#undef CONFIG_SYS_CLOCK_EXISTS
#endif

/* Exercise the implementation, including its static initialization path. */
#include "counter_mcux_rtc.c"

#if defined(TEST_NO_TIMER)
#define CONFIG_SYS_CLOCK_EXISTS 1
#endif

static const struct device test_device = {
	.name = STRINGIFY(TEST_SUITE),
	.config = &mcux_rtc_config_0.info,
	.data = &mcux_rtc_data_0,
	.api = &mcux_rtc_driver_api,
};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&rtc_registers, 0, sizeof(rtc_registers));
	memset(&osc32_registers, 0, sizeof(osc32_registers));
	memset(&sim_registers, 0, sizeof(sim_registers));
	now = 0;
	stable_at = UINT64_MAX;
	read_delay = 0;
	forced_sleep = 0;
	sleep_limit = UINT64_MAX;
	sleep_calls = 0;
	busy_wait_calls = 0;
	irq_enable_calls = 0;
	init_calls = 0;
	oscillator_enable_calls = 0;
	clock_enable_calls = 0;
	lpo_enabled = false;
}

#if !defined(TEST_LPO) && !defined(TEST_NO_OSCE)
ZTEST(TEST_SUITE, test_missing_stable_flag)
{
	int expected = IS_ENABLED(TEST_OSC32) ? -ETIMEDOUT : 0;

	zassert_equal(mcux_rtc_init(&test_device), expected);
	zassert_equal(now, STARTUP_TICKS);
	zassert_equal(init_calls, 1);
	zassert_equal(oscillator_enable_calls, 1);
	zassert_equal(clock_enable_calls, IS_ENABLED(TEST_OSC32));
	zassert_equal(irq_enable_calls, expected == 0 ? 1 : 0);
#if defined(TEST_NO_TIMER)
	zassert_equal(busy_wait_calls, STARTUP_MS);
	zassert_equal(sleep_calls, 0);
#else
	zassert_true(sleep_calls > 0);
	zassert_equal(busy_wait_calls, 0);
#endif
}

#if defined(TEST_OSC32)
ZTEST(TEST_SUITE, test_already_stable)
{
	stable_at = 0;

	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(now, 0);
	zassert_equal(sleep_calls, 0);
	zassert_equal(busy_wait_calls, 0);
	zassert_equal(irq_enable_calls, 1);
}

ZTEST(TEST_SUITE, test_stable_before_deadline)
{
	stable_at = STARTUP_TICKS - 1;

	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(now, stable_at);
	zassert_equal(irq_enable_calls, 1);
}

ZTEST(TEST_SUITE, test_stable_at_deadline)
{
	stable_at = STARTUP_TICKS;

	zassert_equal(mcux_rtc_init(&test_device), -ETIMEDOUT);
	zassert_equal(now, STARTUP_TICKS);
	zassert_equal(irq_enable_calls, 0);
}

#if !defined(TEST_NO_TIMER)
ZTEST(TEST_SUITE, test_stable_after_deadline)
{
	stable_at = STARTUP_TICKS + 1;
	forced_sleep = STARTUP_TICKS + 2;

	zassert_equal(mcux_rtc_init(&test_device), -ETIMEDOUT);
	zassert_true(now > STARTUP_TICKS);
	zassert_equal(sleep_calls, 1);
	zassert_equal(irq_enable_calls, 0);
}

ZTEST(TEST_SUITE, test_deadline_passes_during_ready_read)
{
	stable_at = STARTUP_TICKS;
	read_delay = STARTUP_TICKS + 1;

	zassert_equal(mcux_rtc_init(&test_device), -ETIMEDOUT);
	zassert_equal(sleep_calls, 0);
	zassert_equal(irq_enable_calls, 0);
}
#endif /* !TEST_NO_TIMER */
#elif !defined(TEST_NO_TIMER)
ZTEST(TEST_SUITE, test_early_wakeups_preserve_full_wait)
{
	sleep_limit = 1;

	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(now, STARTUP_TICKS);
	zassert_equal(sleep_calls, STARTUP_TICKS);
	zassert_equal(irq_enable_calls, 1);
}
#endif /* TEST_OSC32 */

#if TEST_MUX != 0
ZTEST(TEST_SUITE, test_external_mux_input_skips_oscillator)
{
#if TEST_MUX == 1
	sim_registers.CHIPCTL = 1U << SIM_CHIPCTL_RTC32KCLKSEL_SHIFT;
#else
	sim_registers.CHIPCTL = 1U << SIM_CHIPCTL_RTC_CLKSEL_SHIFT;
#endif
	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(now, 0);
	zassert_equal(oscillator_enable_calls, 0);
	zassert_equal(clock_enable_calls, 0);
	zassert_equal(irq_enable_calls, 1);
}
#if TEST_MUX == 2
ZTEST(TEST_SUITE, test_alternate_mux_input_skips_oscillator)
{
	sim_registers.CHIPCTL = 2U << SIM_CHIPCTL_RTC_CLKSEL_SHIFT;

	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(now, 0);
	zassert_equal(oscillator_enable_calls, 0);
	zassert_equal(clock_enable_calls, 0);
	zassert_equal(irq_enable_calls, 1);
}
#endif
#endif /* TEST_MUX != 0 */

#else
ZTEST(TEST_SUITE, test_unused_oscillator_is_not_enabled_or_waited_for)
{
	zassert_ok(mcux_rtc_init(&test_device));
	zassert_equal(init_calls, 1);
	zassert_equal(lpo_enabled, IS_ENABLED(TEST_LPO));
	zassert_equal(now, 0);
	zassert_equal(sleep_calls, 0);
	zassert_equal(busy_wait_calls, 0);
	zassert_equal(oscillator_enable_calls, 0);
	zassert_equal(clock_enable_calls, 0);
	zassert_equal(irq_enable_calls, 1);
}
#endif /* !TEST_LPO && !TEST_NO_OSCE */

ZTEST_SUITE(TEST_SUITE, NULL, NULL, before, NULL, NULL);
